// driver_dfsone.cpp (Windows) — serial DFS over prefixes from .pfx file(s).
//
// No MPI: reads the prefixes from the given .pfx batch file(s), searches each
// one exhaustively, and reports the longest snake found within them. This is the
// single-process Windows counterpart of the mac/Linux MPI `dfs_from_files`; give
// it a file holding one prefix (generate with batch_size 1) to search exactly one
// prefix's subtree.
//
// Because it needs no MS-MPI, it builds with only cl.exe.
//
// Usage:
//   dfs_one.exe <batch.pfx> [more.pfx ...]
//
// N and PREFIX_LENGTH are compile-time; a file whose header disagrees is rejected.
// L is the longest snake WITHIN the supplied files, a lower bound on the global
// maximum for Q_N.

#include "config.hpp"
#include "dirutil.hpp"
#include "prefixgen.hpp"
#include "search.hpp"
#include "pfxio.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <batch.pfx> [more.pfx ...]\n", argv[0]);
        return 1;
    }

    std::vector<Prefix> prefixes;
    for (int a = 1; a < argc; a++)
        if (!pfxRead(argv[a], prefixes))
            return 1;

    if (prefixes.empty())
    {
        fprintf(stderr, "dfs_one: no prefixes loaded.\n");
        return 1;
    }
    const size_t nprefix = prefixes.size();
    printf("N=%d, PREFIX_LENGTH=%d, %zu prefixes from %d file(s)\n",
           N, PREFIX_LENGTH, nprefix, argc - 1);

    Search S;

    // Pass 1: longest length within the batch, plus each prefix's max & count.
    std::vector<int>       pmax(nprefix);
    std::vector<long long> pcnt(nprefix);
    int       L = 0;
    long long nodes = 0;
    for (size_t i = 0; i < nprefix; i++)
    {
        S.dfs_from_prefix(prefixes[i].vertices, prefixes[i].transitionCounter);
        pmax[i] = S.maxLength;
        pcnt[i] = S.maxSnakeCounter;
        if (S.maxLength > L) L = S.maxLength;
        nodes += S.vertexCounter;
    }
    long long count1 = 0;
    for (size_t i = 0; i < nprefix; i++)
        if (pmax[i] == L) count1 += pcnt[i];
    printf("Longest snake in batch: %d edges, count = %lld\n", L - 1, count1);

    // Pass 2: emit the length-L snakes to a .bin (same format as the other tools).
    ensureDirRecursive("job_outputs/snakes_dfs_search");
    char path[256];
    snprintf(path, sizeof(path), "job_outputs/snakes_dfs_search/%dD_L%d_dfsone.bin", N, L);
    FILE *bf = fopen(path, "wb");
    if (bf) { int hdr[2] = { N, L }; fwrite(hdr, sizeof(int), 2, bf); }

    S.targetLength = L;
    S.outFile = bf;
    long long count2 = 0;
    for (size_t i = 0; i < nprefix; i++)
        if (pmax[i] == L)
        {
            S.dfs_from_prefix(prefixes[i].vertices, prefixes[i].transitionCounter);
            count2 += S.emitCount;
        }
    if (bf) fclose(bf);

    printf("Snakes written: %lld -> %s\n", count2, path);
    printf("Pass counts %s\n", count1 == count2 ? "match." : "MISMATCH!");
    printf("Search nodes: %lld\n", nodes);
    printf("NOTE: L is the longest within these files, not a global maximum.\n");
    return 0;
}
