// driver_fromfiles.cpp — MPI DFS over prefixes read from .pfx batch files.
//
// This is dfs_search_replay with the prefix source swapped: instead of
// regenerating prefixes with PrefixGen, it loads them from the .pfx files given
// on the command line (this machine's assigned batch). Every rank loads the
// batch, then statically round-robins the prefixes across ranks and runs the
// standard two passes.
//
// There is deliberately NO cross-machine coordination: the L reported here is
// the longest snake WITHIN the supplied batch. Reconciling the global maximum
// across machines is a manual post-step.
//
// Usage:
//   mpirun -n <procs> ./dfs_from_files <batch.pfx> [more.pfx ...]

#include "config.hpp"
#include "prefixgen.hpp"
#include "search.hpp"
#include "pfxio.hpp"

#include <mpi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define MAKEDIR(path) _mkdir(path)
#else
#define MAKEDIR(path) mkdir((path), 0777)
#endif

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
    char path[160];
    for (;;)
    {
        snprintf(path, sizeof(path),
                 "job_outputs/%dD_fromfiles_%d_processes_pl%d_%d.txt",
                 N, procs, PREFIX_LENGTH, run);
        FILE *f = fopen(path, "r");
        if (!f) return run;
        fclose(f);
        run++;
    }
}

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc < 2)
    {
        if (rank == 0)
            fprintf(stderr, "Usage: mpirun -n <procs> %s <batch.pfx> [more.pfx ...]\n", argv[0]);
        MPI_Finalize();
        return 1;
    }

    // Every rank loads the whole batch, then owns prefixes i where i%size==rank.
    std::vector<Prefix> prefixes;
    for (int a = 1; a < argc; a++)
        if (!pfxRead(argv[a], prefixes))
        {
            MPI_Finalize();
            return 1;
        }
    unsigned long long nprefix = prefixes.size();

    if (nprefix == 0)
    {
        if (rank == 0) fprintf(stderr, "dfs_from_files: no prefixes loaded.\n");
        MPI_Finalize();
        return 1;
    }

    if (rank == 0)
        printf("Dimension N = %d, ranks = %d, pl = %d\nBatch: %d file(s), %llu prefixes\n",
               N, size, PREFIX_LENGTH, argc - 1, nprefix);

    double t_start = MPI_Wtime();

    // Pass 1: longest length within the batch.
    Search S;
    std::vector<int>  ownedIdx;
    std::vector<int>  ownedMax;
    long long localNodes = 0;
    for (unsigned long long i = 0; i < nprefix; i++)
    {
        if (i % (unsigned long long)size != (unsigned long long)rank) continue;
        S.dfs_from_prefix(prefixes[i].vertices, prefixes[i].transitionCounter);
        ownedIdx.push_back((int)i);
        ownedMax.push_back(S.maxLength);
        localNodes += S.vertexCounter;
    }
    double pass1Time = MPI_Wtime() - t_start;

    int localL = 0;
    for (int m : ownedMax) if (m > localL) localL = m;
    int L = 0;
    MPI_Allreduce(&localL, &L, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);

    long long localCount1 = 0;
    for (size_t k = 0; k < ownedIdx.size(); k++)
        if (ownedMax[k] == L)
        {
            S.dfs_from_prefix(prefixes[ownedIdx[k]].vertices,
                              prefixes[ownedIdx[k]].transitionCounter);
            localCount1 += S.maxSnakeCounter;
        }
    long long count1 = 0, totalNodes = 0;
    MPI_Allreduce(&localCount1, &count1,     1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allreduce(&localNodes,  &totalNodes, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);

    if (rank == 0)
        printf("Pass 1 done: longest length in batch = %d edges, count = %lld\n",
               L - 1, count1);

    // Pass 2: emit every length-L snake this rank owns.
    double t_pass2 = MPI_Wtime();
    MAKEDIR("job_outputs"); MAKEDIR("job_outputs/snakes_dfs_search");

    char binPath[128];
    snprintf(binPath, sizeof(binPath),
             "job_outputs/snakes_dfs_search/%dD_L%d_fromfiles_rank%d.bin", N, L, rank);
    FILE *bf = fopen(binPath, "wb");
    if (bf) { int hdr[2] = { N, L }; fwrite(hdr, sizeof(int), 2, bf); }

    S.targetLength = L;
    S.outFile = bf;

    long long localCount2 = 0;
    for (size_t k = 0; k < ownedIdx.size(); k++)
        if (ownedMax[k] == L)
        {
            S.dfs_from_prefix(prefixes[ownedIdx[k]].vertices,
                              prefixes[ownedIdx[k]].transitionCounter);
            localCount2 += S.emitCount;
        }
    if (bf) fclose(bf);

    long long count2 = 0;
    MPI_Allreduce(&localCount2, &count2, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
    double pass2Time = MPI_Wtime() - t_pass2;
    double elapsed   = MPI_Wtime() - t_start;

    if (rank == 0)
    {
        int run = nextRunIndex(size);
        char rpath[176];
        snprintf(rpath, sizeof(rpath),
                 "job_outputs/%dD_fromfiles_%d_processes_pl%d_%d.txt",
                 N, size, PREFIX_LENGTH, run);
        FILE *rr = fopen(rpath, "w");
        if (rr)
        {
            fprintf(rr, "Runtime = %.5f hours\n"
                        "Pass-1 (search) time = %.6f seconds\n"
                        "Pass-2 (emit) time = %.6f seconds\n"
                        "Mode = from-files (batch is NOT the whole search)\n"
                        "Batch files = %d\n"
                        "Prefixes in batch = %llu\n"
                        "Number of max snakes found = %lld\n"
                        "Longest snake in batch = %d edges\n",
                    elapsed / 3600.0, pass1Time, pass2Time,
                    argc - 1, nprefix, count2, L - 1);
            fprintf(rr, "Snake paths:\n---\n");
            for (int r = 0; r < size; r++)
            {
                char bp[128];
                snprintf(bp, sizeof(bp),
                         "job_outputs/snakes_dfs_search/%dD_L%d_fromfiles_rank%d.bin", N, L, r);
                decodeInto(rr, bp);
            }
            fprintf(rr, "---\n");
            fclose(rr);
        }

        double meanCost = totalNodes ? (pass1Time / (double)totalNodes) : 0.0;
        printf("\n");
        printf("Longest snake in batch (edges): %d\n", L - 1);
        printf("Total longest snakes written:   %lld\n", count2);
        printf("Pass-1 count (sanity check):    %lld %s\n", count1,
               count1 == count2 ? "(matches)" : "(MISMATCH!)");
        printf("Elapsed time:                   %.6f seconds\n", elapsed);
        printf("  pass-1 (search) time:         %.6f seconds\n", pass1Time);
        printf("  pass-2 (emit) time:           %.6f seconds\n", pass2Time);
        printf("Search: pass-1 nodes = %lld, mean cost/node = %.3e s\n",
               totalNodes, meanCost);
        printf("NOTE: L is the longest within THIS batch, not a global maximum.\n");
    }

    MPI_Finalize();
    return 0;
}
