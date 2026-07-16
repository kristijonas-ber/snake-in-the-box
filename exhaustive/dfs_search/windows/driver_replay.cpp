/* driver_replay.cpp — worker-side replay: dispatcher-free, parallel generation.
 *
 * Contrast with driver_main.cpp (the dispatcher build): there, rank 0 alone
 * walks the canonical tree — three serial walks — and streams prefixes to
 * workers. Here EVERY rank regenerates the deterministic tree itself and
 * computes only its own stripe of prefixes. Because generation is deterministic,
 * no master is needed: rank r owns in-slice prefix number j iff j % size == r.
 *
 *   + Generation is parallel (all ranks walk concurrently → one walk of wall
 *     time, not a serial dispatch) and there is zero task messaging — the only
 *     communication is two MPI_Allreduce calls.
 *   - Assignment is STATIC round-robin, so there is no dynamic load balancing
 *     (a straggler prefix idles the ranks that finished). Accepted trade-off:
 *     round-robin over canonical order interleaves heavy/light prefixes.
 *
 * Reuses PrefixGen (prefixgen.hpp) and Search (search.hpp) unchanged.
 *
 * No COUNT pass, no nrec-sized dense arrays, no checkpoint (v1): per-rank state
 * is O(owned prefixes). Two passes (find L, emit-all), same as the dispatcher.
 */
#include "config.hpp"
#include "dirutil.hpp"
#include "prefixgen.hpp"
#include "search.hpp"

#include <mpi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
#include <sys/stat.h>

/* ----- decode helpers (same format as driver_main.cpp) ----- */
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

/* Per-owned-prefix record kept locally on each rank (sized to owned count). */
struct OwnedRec {
    unsigned long long absOrdinal;
    int    maxLength;
    int    maxSnakeCounter;
    int    searchSpace;
    double runtime;
};

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

    if (rank == 0)
    {
        printf("Dimension N = %d, ranks = %d, pl = %d  (worker-side replay)\n",
               N, size, PREFIX_LENGTH);
#if SLICE_COUNT > 1
        printf("Slice mode: id %d of %d (stripe)\n", SLICE_ID, SLICE_COUNT);
#else
        printf("Window mode: offset %d, count %d\n", ROOT_OFFSET, ROOT_COUNT);
#endif
        fflush(stdout);
    }

    double t_start = MPI_Wtime();

    /* ============== PASS 1: each rank searches its owned stripe, find L ==============
     *
     * The generator callback fires once per in-slice prefix, in canonical order.
     * `jInSlice` is that prefix's index among in-slice prefixes; this rank owns it
     * iff jInSlice % size == rank. Only owned prefixes are searched. */
    Search S;
    std::vector<OwnedRec> owned;
    unsigned long long jInSlice = 0;      /* index among in-slice prefixes */
    unsigned long long totalRoots = 0;    /* all prefixes (== g.ordinal)   */

    {
        PrefixGen g;
        g.generate([&](const int *verts, int tc, unsigned long long absIdx) {
            unsigned long long j = jInSlice++;
            if (j % (unsigned long long)size == (unsigned long long)rank)
            {
                double s = MPI_Wtime();
                S.dfs_from_prefix(verts, tc);
                double rt = MPI_Wtime() - s;
                OwnedRec r;
                r.absOrdinal      = absIdx;
                r.maxLength       = S.maxLength;
                r.maxSnakeCounter = S.maxSnakeCounter;
                r.searchSpace     = (int)S.vertexCounter;
                r.runtime         = rt;
                owned.push_back(r);
            }
            return true;
        });
        totalRoots = g.ordinal;
    }
    unsigned long long nrec = jInSlice;   /* in-slice total (same on every rank) */

    double pass1Time = MPI_Wtime() - t_start;

    /* global longest length L = max over all owned maxima */
    int localL = 0;
    for (const OwnedRec &r : owned) if (r.maxLength > localL) localL = r.maxLength;
    int L = 0;
    MPI_Allreduce(&localL, &L, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);

    /* count of length-L maxima (pass-1) + node totals, summed across ranks */
    long long localCount1 = 0, localNodes = 0;
    for (const OwnedRec &r : owned)
    {
        if (r.maxLength == L) localCount1 += r.maxSnakeCounter;
        localNodes += r.searchSpace;
    }
    long long count1 = 0, totalNodes = 0;
    MPI_Allreduce(&localCount1, &count1,     1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allreduce(&localNodes,  &totalNodes, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);

    bool partial = (nrec < totalRoots);

    if (rank == 0)
        printf("Pass 1 done: longest length = %d edges, count = %lld%s\n",
               L - 1, count1, partial ? "   (LOWER BOUND — partial slice)" : "");

    /* ============== PASS 2: re-walk, emit every length-L snake from owned hits ==============
     *
     * Re-run the generator with the identical assignment. `owned` is in canonical
     * (walk) order, so we index it with a running owned-counter to recover each
     * owned prefix's pass-1 maxLength without recomputing it. */
    double t_pass2 = MPI_Wtime();
    if (!ensureDirRecursive("job_outputs/snakes_dfs_search"))
    {
        fprintf(stderr, "cannot create output dir job_outputs/snakes_dfs_search\n");
        MPI_Finalize();
        return 1;
    }

    char binPath[96];
    snprintf(binPath, sizeof(binPath),
             "job_outputs/snakes_dfs_search/%dD_L%d_rank%d.bin", N, L, rank);
    FILE *bf = fopen(binPath, "wb");
    if (bf) { int hdr[2] = { N, L }; fwrite(hdr, sizeof(int), 2, bf); }

    S.targetLength = L;
    S.outFile = bf;

    long long localCount2 = 0;
    {
        size_t a = 0;                       /* owned index, advanced per owned prefix */
        unsigned long long j2 = 0;
        PrefixGen g;
        g.generate([&](const int *verts, int tc, unsigned long long /*absIdx*/) {
            unsigned long long j = j2++;
            if (j % (unsigned long long)size == (unsigned long long)rank)
            {
                if (owned[a].maxLength == L)
                {
                    S.dfs_from_prefix(verts, tc);
                    localCount2 += S.emitCount;
                }
                a++;
            }
            return true;
        });
    }
    if (bf) fclose(bf);

    long long count2 = 0;
    MPI_Allreduce(&localCount2, &count2, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
    double pass2Time = MPI_Wtime() - t_pass2;
    double elapsed   = MPI_Wtime() - t_start;

    /* ============== reports ============== */
    int run = 0;
    if (rank == 0) run = nextRunIndex(size);
    MPI_Bcast(&run, 1, MPI_INT, 0, MPI_COMM_WORLD);

    /* per-rank per-process fragment (avoids a Gatherv) */
    char ppath[176];
    snprintf(ppath, sizeof(ppath),
             "job_outputs/%dD_resultsPerProcess_%d_processes_pl%d_%d_rank%d.txt",
             N, size, PREFIX_LENGTH, run, rank);
    FILE *pp = fopen(ppath, "w");
    if (pp)
    {
        for (const OwnedRec &r : owned)
            fprintf(pp,
                "--- Process %d, prefix %llu ---\n"
                "Runtime = %.10f seconds\n"
                "Number of max snakes found = %d\n"
                "Maximum length snake = %d\n"
                "Search space (vertices) = %d\n"
                "**********\n",
                rank, r.absOrdinal, r.runtime, r.maxSnakeCounter,
                r.maxLength - 1, r.searchSpace);
        fclose(pp);
    }

    if (rank == 0)
    {
        char mpath[96];
        snprintf(mpath, sizeof(mpath),
                 "job_outputs/snakes_dfs_search/%dD_L%d_manifest.txt", N, L);
        FILE *m = fopen(mpath, "w");
        if (m)
        {
            fprintf(m, "N %d\nlength_edges %d\ntotal %lld\nslice %d/%d roots %llu of %llu (replay)\n",
                    N, L - 1, count2, SLICE_ID, SLICE_COUNT, nrec, totalRoots);
            fclose(m);
        }

        char rpath[128];
        snprintf(rpath, sizeof(rpath),
                 "job_outputs/%dD_results_%d_processes_pl%d_%d.txt",
                 N, size, PREFIX_LENGTH, run);
        FILE *rr = fopen(rpath, "w");
        if (rr)
        {
            fprintf(rr, "Runtime = %.5f hours\n"
                        "Pass-1 (search) time = %.6f seconds\n"
                        "Pass-2 (emit) time = %.6f seconds\n"
                        "Mode = worker-side replay (static round-robin)\n"
                        "Slice = %d of %d\n"
                        "Roots computed = %llu of %llu\n"
                        "Number of max snakes found = %lld\n"
                        "Maximum length snake is: %d%s\n",
                    elapsed / 3600.0, pass1Time, pass2Time,
                    SLICE_ID, SLICE_COUNT, nrec, totalRoots, count2, L - 1,
                    partial ? "  (LOWER BOUND)" : "");
            fprintf(rr, "Snake paths:\n---\n");
            for (int r = 0; r < size; r++)
            {
                char bp[96];
                snprintf(bp, sizeof(bp),
                         "job_outputs/snakes_dfs_search/%dD_L%d_rank%d.bin", N, L, r);
                decodeInto(rr, bp);
            }
            fprintf(rr, "---\n");
            fclose(rr);
        }

        double meanCost = totalNodes ? (pass1Time / (double)totalNodes) : 0.0;

        printf("\n");
        printf("Longest snake length (edges): %d%s\n", L - 1,
               partial ? "  (lower bound — partial slice)" : "");
        printf("Total longest snakes written: %lld\n", count2);
        printf("Pass-1 count (sanity check):  %lld %s\n", count1,
               count1 == count2 ? "(matches)" : "(MISMATCH!)");
        printf("Roots computed:               %llu of %llu\n", nrec, totalRoots);
        printf("Elapsed time:                 %.6f seconds\n", elapsed);
        printf("  pass-1 (search) time:       %.6f seconds\n", pass1Time);
        printf("  pass-2 (emit) time:         %.6f seconds\n", pass2Time);
        printf("Search: pass-1 nodes = %lld, mean cost/node = %.3e s\n",
               totalNodes, meanCost);
        if (partial)
        {
            double scale = nrec ? (double)totalRoots / (double)nrec : 0.0;
            printf("Partial-run wall estimate: %.6f seconds for all %llu prefixes at this rank count\n",
                   pass1Time * scale, totalRoots);
        }
    }

    MPI_Finalize();
    return 0;
}
