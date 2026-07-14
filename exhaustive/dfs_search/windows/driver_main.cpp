/* driver_main.cpp — MPI orchestration for dfs_search.
 *
 * Rank 0 is the dispatcher; ranks 1..n-1 are workers. The novelty vs v13 is
 * that rank 0 NEVER builds a prefix table. It drives the streaming PrefixGen and,
 * at each in-slice prefix, hands that single prefix straight to a free worker.
 * Resident rank-0 RAM is O(numWorkers + nrec) where nrec = #prefixes THIS run
 * computes (its slice) — it never scales with the total prefix universe.
 *
 * Passes (each a full deterministic generator walk; the slice filter selects
 * what to act on):
 *   COUNT : collect the absolute ordinal of every in-slice prefix (-> nrec,
 *           totalRoots). O(nrec) longs, no prefixes stored.
 *   PASS1 : dispatch each in-slice prefix, find longest length L.
 *   PASS2 : dispatch only the prefixes that reached L, stream all length-L snakes.
 */
#include "config.hpp"
#include "prefixgen.hpp"
#include "search.hpp"

#include <mpi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define MAKEDIR(path) _mkdir(path)
#else
#define MAKEDIR(path) mkdir((path), 0777)
#endif

/* ----- decode helpers (unchanged from v13) ----- */
static int decodeFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    int hdr[2];
    if (fread(hdr, sizeof(int), 2, f) != 2) { fclose(f); return 1; }
    int steps = hdr[1] - 1;
    unsigned char *buf = (unsigned char *)malloc((size_t)steps);
    while (fread(buf, 1, (size_t)steps, f) == (size_t)steps)
    {
        int v = 0;
        printf("%d ", v);
        for (int i = 0; i < steps; i++) { v ^= (1 << buf[i]); printf("%d ", v); }
        printf("\n");
    }
    free(buf);
    fclose(f);
    return 0;
}

static void decodeInto(FILE *out, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return;
    int hdr[2];
    if (fread(hdr, sizeof(int), 2, f) != 2) { fclose(f); return; }
    int steps = hdr[1] - 1;
    unsigned char *buf = (unsigned char *)malloc((size_t)steps);
    while (fread(buf, 1, (size_t)steps, f) == (size_t)steps)
    {
        int v = 0; fprintf(out, "%d ", v);
        for (int i = 0; i < steps; i++) { v ^= (1 << buf[i]); fprintf(out, "%d ", v); }
        fprintf(out, "\n");
    }
    free(buf);
    fclose(f);
}

static void drawProgress(const char *label, int done, int total, int redraw)
{
    int filled = total ? (done * BAR_WIDTH) / total : BAR_WIDTH;
    int pct    = total ? (done * 100) / total : 100;
    if (redraw) printf("\033[1A\033[2K");
    printf("\r[");
    for (int i = 0; i < BAR_WIDTH; i++)
        putchar(i < filled ? '#' : '-');
    printf("] %s %d/%d (%d%%)\n", label, done, total, pct);
    fflush(stdout);
}

/* ----- checkpoint (dense arrays sized to nrec; header pins the slice config) ----- */
static void saveCheckpoint(const char *path, int nrec, const int *done,
                           const int *prefixMax, const int *prefixCount)
{
    char tmp[96];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "N %d prefixlen %d slice %d %d window %d %d nrec %d\n",
            N, PREFIX_LENGTH, SLICE_ID, SLICE_COUNT, ROOT_OFFSET, ROOT_COUNT, nrec);
    for (int i = 0; i < nrec; i++)
        fprintf(f, "%d %d %d\n", done[i], prefixMax[i], prefixCount[i]);
    fclose(f);
#ifdef _WIN32
    remove(path);           /* Windows rename() fails if the target exists */
#endif
    rename(tmp, path);
}

static int loadCheckpoint(const char *path, int nrec, int *done,
                          int *prefixMax, int *prefixCount)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int cn, cpl, csid, csc, croff, crc, cnr;
    if (fscanf(f, "N %d prefixlen %d slice %d %d window %d %d nrec %d",
               &cn, &cpl, &csid, &csc, &croff, &crc, &cnr) != 7
        || cn != N || cpl != PREFIX_LENGTH || csid != SLICE_ID || csc != SLICE_COUNT
        || croff != ROOT_OFFSET || crc != ROOT_COUNT || cnr != nrec)
    {
        fclose(f);
        return 0;
    }
    for (int i = 0; i < nrec; i++)
        if (fscanf(f, "%d %d %d", &done[i], &prefixMax[i], &prefixCount[i]) != 3)
        {
            fclose(f);
            return 0;
        }
    fclose(f);
    return 1;
}

/* ===================== rank-0 streaming dispatcher ===================== */
struct Dispatcher {
    int  numWorkers;
    int  nrec;
    bool recordEnabled;      /* pass 1 records per-prefix data; pass 2 does not */
    int  L;                  /* pass 2: dispatch only slots with prefixMax==L   */
    bool pass2;

    /* per-prefix arrays (sized nrec, local slot indexed) */
    int    *prefixMax, *prefixCount, *prefixSearch, *prefixProc, *done;
    double *prefixRuntime, *prefixKnuth;
    long long *perRank;
    const char *ckptPath;

    std::vector<int> lastSent;   /* lastSent[src] = slot last handed to worker */
    int    slot      = 0;
    int    completed = 0;
    double lastDraw, lastCkpt;
    const char *label;

    void init(int nw)
    {
        numWorkers = nw;
        lastSent.assign((size_t)nw + 1, 0);
        slot = 0; completed = 0;
        lastDraw = lastCkpt = MPI_Wtime();
    }

    void handleResult(int src, double *resd)
    {
        int rec = lastSent[src];
        if (recordEnabled)
        {
            prefixMax[rec]     = (int)resd[0];
            prefixCount[rec]   = (int)resd[1];
            prefixSearch[rec]  = (int)resd[2];
            prefixRuntime[rec] = resd[3];
            prefixKnuth[rec]   = resd[4];
            prefixProc[rec]    = src;
            done[rec]          = 1;
        }
        if (perRank) perRank[src] += (long long)resd[1];

        completed++;
        double now = MPI_Wtime();
        if (now - lastDraw >= 0.1 || completed == nrec)
        {
            drawProgress(label, completed, nrec, 1);
            lastDraw = now;
        }
        if (ckptPath && now - lastCkpt >= CKPT_PERIOD)
        {
            saveCheckpoint(ckptPath, nrec, done, prefixMax, prefixCount);
            lastCkpt = now;
        }
    }

    /* Block until a worker asks for work, draining any results that arrive. */
    int getFreeWorker()
    {
        double resd[RESULT_DBLS];
        for (;;)
        {
            MPI_Status st;
            MPI_Recv(resd, RESULT_DBLS, MPI_DOUBLE, MPI_ANY_SOURCE, MPI_ANY_TAG,
                     MPI_COMM_WORLD, &st);
            if (st.MPI_TAG == TAG_RESULT) handleResult(st.MPI_SOURCE, resd);
            else return st.MPI_SOURCE;   /* TAG_REQUEST */
        }
    }

    /* Called (via the generator callback) once per in-slice prefix, in order. */
    bool onLeaf(const int *verts, int tc, unsigned long long /*ordinal*/)
    {
        int s = slot++;
        if (pass2) { if (prefixMax[s] != L) return true; }   /* not a hit → skip */
        else       { if (done[s])           return true; }   /* resume → already done */

        int src = getFreeWorker();
        int taskBuf[TASK_INTS];
        for (int i = 0; i < PREFIX_LENGTH; i++) taskBuf[i] = verts[i];
        taskBuf[PREFIX_LENGTH] = tc;
        lastSent[src] = s;
        MPI_Send(taskBuf, TASK_INTS, MPI_INT, src, TAG_TASK, MPI_COMM_WORLD);
        return true;
    }

    /* Generation finished: collect in-flight results, stop every worker. */
    void drain()
    {
        int active = numWorkers;
        double resd[RESULT_DBLS];
        int dummy = 0;
        while (active > 0)
        {
            MPI_Status st;
            MPI_Recv(resd, RESULT_DBLS, MPI_DOUBLE, MPI_ANY_SOURCE, MPI_ANY_TAG,
                     MPI_COMM_WORLD, &st);
            if (st.MPI_TAG == TAG_RESULT) handleResult(st.MPI_SOURCE, resd);
            else { MPI_Send(&dummy, 1, MPI_INT, st.MPI_SOURCE, TAG_STOP, MPI_COMM_WORLD); active--; }
        }
    }
};

/* ===================== worker ===================== */
static void worker(Search &S, int targetLen, FILE *f)
{
    S.targetLength = targetLen;
    S.outFile = f;

    int taskBuf[TASK_INTS];
    double resd[RESULT_DBLS] = {0};
    while (1)
    {
        MPI_Send(resd, RESULT_DBLS, MPI_DOUBLE, 0, TAG_REQUEST, MPI_COMM_WORLD);

        MPI_Status st;
        MPI_Recv(taskBuf, TASK_INTS, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &st);
        if (st.MPI_TAG == TAG_STOP) break;

        int tc = taskBuf[PREFIX_LENGTH];

        double knuth = 0.0;
#if PROBE_ONLY
        double s_probe = MPI_Wtime();
        if (targetLen < 0 && KNUTH_PROBES > 0)
            knuth = S.knuth_estimate_prefix(taskBuf, tc, KNUTH_PROBES);
        double probe_time = MPI_Wtime() - s_probe;
        resd[0] = 0; resd[1] = 0; resd[2] = knuth; resd[3] = probe_time; resd[4] = knuth;
#else
        if (targetLen < 0 && KNUTH_PROBES > 0)
            knuth = S.knuth_estimate_prefix(taskBuf, tc, KNUTH_PROBES);
        double s = MPI_Wtime();
        S.dfs_from_prefix(taskBuf, tc);
        double rt = MPI_Wtime() - s;

        resd[0] = S.maxLength;
        resd[1] = (targetLen > 0) ? (double)S.emitCount : (double)S.maxSnakeCounter;
        resd[2] = (double)S.vertexCounter;
        resd[3] = rt;
        resd[4] = knuth;
#endif
        MPI_Send(resd, RESULT_DBLS, MPI_DOUBLE, 0, TAG_RESULT, MPI_COMM_WORLD);
    }
}

static int nextRunIndex(int procs)
{
    int run = 1;
    char path[128];
    for (;;)
    {
        snprintf(path, sizeof(path),
                 "job_outputs/%dD_results_%d_processes_pl%d_%d.txt",
                 N, procs, PREFIX_LENGTH, run);
        FILE *f = fopen(path, "r");
        if (!f) return run;
        fclose(f);
        run++;
    }
}

/* ===================== main ===================== */
int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc > 2 && strcmp(argv[1], "--decode") == 0)
    {
        int rc = (rank == 0) ? decodeFile(argv[2]) : 0;
        MPI_Finalize();
        return rc;
    }

    double t_start = MPI_Wtime();

    if (rank == 0)
    {
        int numWorkers = size - 1;
        if (numWorkers < 1)
        {
            fprintf(stderr, "Need at least 2 processes (1 root + 1 worker).\n");
            MPI_Finalize();
            return 1;
        }

        printf("Dimension N = %d, workers = %d, pl = %d\n", N, numWorkers, PREFIX_LENGTH);
#if SLICE_COUNT > 1
        printf("Slice mode: id %d of %d (stripe)\n", SLICE_ID, SLICE_COUNT);
#else
        printf("Window mode: offset %d, count %d\n", ROOT_OFFSET, ROOT_COUNT);
#endif

        /* ---- COUNT pass: full walk, record in-slice ordinals + total ---- */
        std::vector<unsigned long long> absIdx;
        unsigned long long totalRoots = 0;
        {
            PrefixGen g;
            g.generate([&](const int *, int, unsigned long long idx) {
                absIdx.push_back(idx);
                return true;
            });
            totalRoots = g.ordinal;
        }
        int nrec = (int)absIdx.size();
        printf("Prefixes: %llu total, this slice computes %d\n\n",
               totalRoots, nrec);

        int    *done         = (int *)   calloc((size_t)nrec ? nrec : 1, sizeof(int));
        int    *prefixMax    = (int *)   calloc((size_t)nrec ? nrec : 1, sizeof(int));
        int    *prefixCount  = (int *)   calloc((size_t)nrec ? nrec : 1, sizeof(int));
        int    *prefixSearch = (int *)   calloc((size_t)nrec ? nrec : 1, sizeof(int));
        double *prefixRuntime= (double *)calloc((size_t)nrec ? nrec : 1, sizeof(double));
        double *prefixKnuth  = (double *)calloc((size_t)nrec ? nrec : 1, sizeof(double));
        int    *prefixProc   = (int *)   calloc((size_t)nrec ? nrec : 1, sizeof(int));

        char ckpt[64];
        snprintf(ckpt, sizeof(ckpt), "checkpoint_%d_dfs_search.txt", N);
        if (loadCheckpoint(ckpt, nrec, done, prefixMax, prefixCount))
            printf("(RESUMED from checkpoint)\n");

        Dispatcher disp;
        disp.init(numWorkers);
        disp.nrec = nrec;
        disp.prefixMax = prefixMax;   disp.prefixCount = prefixCount;
        disp.prefixSearch = prefixSearch; disp.prefixRuntime = prefixRuntime;
        disp.prefixKnuth = prefixKnuth;   disp.prefixProc = prefixProc;
        disp.done = done; disp.perRank = nullptr;

        /* ============== PASS 1 ============== */
        t_start = MPI_Wtime();
        disp.recordEnabled = true;
        disp.pass2 = false;
        disp.ckptPath = ckpt;
        disp.label = "pass-1 prefixes";
        disp.slot = 0; disp.completed = 0;
        printf("Pass 1 — finding longest length (%d prefixes):\n", nrec);
        drawProgress(disp.label, 0, nrec, 0);
        {
            PrefixGen g;
            g.generate([&](const int *v, int tc, unsigned long long idx) {
                return disp.onLeaf(v, tc, idx);
            });
        }
        disp.drain();
        double pass1Time = MPI_Wtime() - t_start;

        int L = 0;
        long long count1 = 0;
        for (int s = 0; s < nrec; s++) if (prefixMax[s] > L) L = prefixMax[s];
        for (int s = 0; s < nrec; s++) if (prefixMax[s] == L) count1 += prefixCount[s];

        bool partial = ((unsigned long long)nrec < totalRoots);
        printf("  longest length = %d edges, count = %lld%s\n\n", L - 1, count1,
               partial ? "   (LOWER BOUND — partial slice)" : "");

        MPI_Bcast(&L, 1, MPI_INT, 0, MPI_COMM_WORLD);

        /* ============== PASS 2 ============== */
        long long *perRank = (long long *)calloc((size_t)size, sizeof(long long));
        double t_pass2 = MPI_Wtime();

#if !PROBE_ONLY
        int hitLen = 0;
        for (int s = 0; s < nrec; s++) if (prefixMax[s] == L) hitLen++;
        printf("Pass 2 — writing all longest snakes (%d of %d prefixes hit L):\n",
               hitLen, nrec);

        disp.recordEnabled = false;
        disp.pass2 = true;
        disp.L = L;
        disp.perRank = perRank;
        disp.ckptPath = nullptr;
        disp.label = "pass-2 emit";
        disp.slot = 0; disp.completed = 0; disp.nrec = hitLen ? hitLen : 1;
        if (hitLen > 0)
        {
            drawProgress(disp.label, 0, hitLen, 0);
            PrefixGen g;
            g.generate([&](const int *v, int tc, unsigned long long idx) {
                return disp.onLeaf(v, tc, idx);
            });
        }
        disp.drain();
#else
        printf("PROBE-ONLY: skipping pass 2 (no real DFS was run).\n");
#endif
        double pass2Time = MPI_Wtime() - t_pass2;

        double elapsed = MPI_Wtime() - t_start;
        long long count2 = 0;
        for (int r = 1; r < size; r++) count2 += perRank[r];

#if !PROBE_ONLY
        MAKEDIR("job_outputs"); MAKEDIR("job_outputs/snakes_dfs_search");
        char mpath[96];
        snprintf(mpath, sizeof(mpath), "job_outputs/snakes_dfs_search/%dD_L%d_manifest.txt", N, L);
        FILE *m = fopen(mpath, "w");
        if (m)
        {
            fprintf(m, "N %d\nlength_edges %d\ntotal %lld\nslice %d/%d roots %d of %llu\n",
                    N, L - 1, count2, SLICE_ID, SLICE_COUNT, nrec, totalRoots);
            for (int r = 1; r < size; r++)
                fprintf(m, "rank %d count %lld\n", r, perRank[r]);
            fclose(m);
        }
#endif

        int run = nextRunIndex(size);
        char rpath[128], ppath[144];
        snprintf(rpath, sizeof(rpath),
                 "job_outputs/%dD_results_%d_processes_pl%d_%d.txt",
                 N, size, PREFIX_LENGTH, run);
        snprintf(ppath, sizeof(ppath),
                 "job_outputs/%dD_resultsPerProcess_%d_processes_pl%d_%d.txt",
                 N, size, PREFIX_LENGTH, run);

        FILE *pp = fopen(ppath, "w");
        if (pp)
        {
            for (int s = 0; s < nrec; s++)
            {
                double k = prefixKnuth[s];
                double ratio = (prefixSearch[s] > 0) ? k / (double)prefixSearch[s] : 0.0;
                fprintf(pp,
                    "--- Process %d, prefix %llu ---\n"
                    "Runtime = %.10f seconds\n"
                    "Number of max snakes found = %d\n"
                    "Maximum length snake = %d\n"
                    "Search space (vertices) = %d\n"
                    "Knuth estimate (%d probes) = %.2f  (ratio Knuth/search = %.3fx)\n"
                    "**********\n",
                    prefixProc[s], absIdx[s],
                    prefixRuntime[s], prefixCount[s],
                    prefixMax[s] - 1, prefixSearch[s],
                    KNUTH_PROBES, k, ratio);
            }
            fclose(pp);
        }

        FILE *rr = fopen(rpath, "w");
        if (rr)
        {
            fprintf(rr, "Runtime = %.5f hours\n"
                        "Pass-1 (search) time = %.6f seconds\n"
                        "Pass-2 (emit) time = %.6f seconds\n"
                        "Slice = %d of %d\n"
                        "Roots computed = %d of %llu\n"
                        "Number of max snakes found = %lld\n"
                        "Maximum length snake is: %d%s\n",
                    elapsed / 3600.0, pass1Time, pass2Time,
                    SLICE_ID, SLICE_COUNT,
                    nrec, totalRoots, count2, L - 1,
                    partial ? "  (LOWER BOUND)" : "");
#if !PROBE_ONLY
            fprintf(rr, "Snake paths:\n---\n");
            for (int r = 1; r < size; r++)
            {
                char bp[96];
                snprintf(bp, sizeof(bp), "job_outputs/snakes_dfs_search/%dD_L%d_rank%d.bin", N, L, r);
                decodeInto(rr, bp);
            }
            fprintf(rr, "---\n");
#endif
            fclose(rr);
        }

        remove(ckpt);

        double totalNodes = 0.0, totalKnuth = 0.0;
        for (int s = 0; s < nrec; s++) totalNodes += prefixSearch[s];
        for (int s = 0; s < nrec; s++) totalKnuth += prefixKnuth[s];
        double meanCost     = totalNodes ? (pass1Time / totalNodes) : 0.0;
        double sampleScale  = nrec ? (double)totalRoots / (double)nrec : 0.0;
        double estFullPass1 = pass1Time * sampleScale;
        double estFullKnuth = totalKnuth * sampleScale;

        free(perRank); free(done); free(prefixMax); free(prefixCount);
        free(prefixSearch); free(prefixRuntime); free(prefixKnuth); free(prefixProc);

        printf("\n");
#if !PROBE_ONLY
        printf("Longest snake length (edges): %d%s\n", L - 1,
               partial ? "  (lower bound — partial slice)" : "");
        printf("Total longest snakes written: %lld\n", count2);
        printf("Pass-1 count (sanity check):  %lld %s\n", count1,
               count1 == count2 ? "(matches)" : "(MISMATCH!)");
#else
        (void)count1; (void)count2;
#endif
        printf("Elapsed time:                 %.6f seconds\n", elapsed);
        printf("  pass-1 (search) time:       %.6f seconds\n", pass1Time);
        printf("  pass-2 (emit) time:         %.6f seconds\n", pass2Time);
        printf("Search: pass-1 nodes = %.0f, mean cost/node = %.3e s\n",
               totalNodes, meanCost);
#if PROBE_ONLY
        printf("Probe estimate: sampled Knuth nodes = %.2f, extrapolated all-prefix = %.2f\n",
               totalKnuth, estFullKnuth);
        printf("Probe-only wall: sampled %.6f s; all-prefix estimate = %.6f s at this MPI size\n",
               pass1Time, estFullPass1);
#else
        (void)estFullKnuth;
        if (partial)
            printf("Partial-run wall estimate: %.6f seconds (%.5f hours) for all %llu prefixes at this MPI size\n",
                   estFullPass1, estFullPass1 / 3600.0, totalRoots);
#endif
    }
    else
    {
        srand((unsigned)time(NULL) ^ ((unsigned)(rank + 1) * 2654435761u));

        Search S;
        worker(S, -1, NULL);

        int L;
        MPI_Bcast(&L, 1, MPI_INT, 0, MPI_COMM_WORLD);

#if !PROBE_ONLY
        MAKEDIR("job_outputs"); MAKEDIR("job_outputs/snakes_dfs_search");
        char path[96];
        snprintf(path, sizeof(path), "job_outputs/snakes_dfs_search/%dD_L%d_rank%d.bin", N, L, rank);
        FILE *f = fopen(path, "wb");
        if (f) { int hdr[2] = { N, L }; fwrite(hdr, sizeof(int), 2, f); }

        worker(S, L, f);
        if (f) fclose(f);
#else
        (void)L;
#endif
    }

    MPI_Finalize();
    return 0;
}
