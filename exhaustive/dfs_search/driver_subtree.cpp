// driver_subtree.cpp — exhaustively search ONE subtree from a chosen prefix.
//
// You name a subset of the search space by its root path — the snake's first few
// transitions — and this searches every snake extending it, reporting the longest.
// No prefix enumeration, no MPI: it seeds the DFS directly and searches that one
// subtree. The result is the exact maximum WITHIN the subtree (a lower bound on
// the global maximum for Q_N).
//
// Usage:
//   ./subtree_search [t1 t2 t3 ...]      transitions (bit positions), canonical order
//   ./subtree_search                     no args = the whole canonical tree
//
// Transitions must be canonical: dimension k may only appear once 0..k-1 have.
// N is compile-time (config.hpp).

#include "config.hpp"
#include "search.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define MAKEDIR(p) _mkdir(p)
#else
#define MAKEDIR(p) mkdir((p), 0777)
#endif

int main(int argc, char **argv)
{
    int ntrans = argc - 1;
    if (ntrans >= MAX_LENGTH)
    {
        fprintf(stderr, "subtree_search: prefix too long (max %d transitions)\n", MAX_LENGTH - 1);
        return 1;
    }

    // Decode transitions -> vertices, deriving (and checking) the canonical counter.
    int verts[MAX_LENGTH];
    verts[0] = 0;
    int tc = 0;
    for (int i = 0; i < ntrans; i++)
    {
        int t = atoi(argv[i + 1]);
        if (t < 0 || t >= N)
        {
            fprintf(stderr, "subtree_search: transition %d out of range [0,%d)\n", t, N);
            return 1;
        }
        if (t > tc)
        {
            fprintf(stderr, "subtree_search: non-canonical prefix — dimension %d used "
                            "before 0..%d are\n", t, tc - 1);
            return 1;
        }
        if (t == tc) tc++;
        verts[i + 1] = verts[i] ^ (1 << t);
    }
    int k = ntrans + 1;                          // vertices in the prefix

    // The prefix must itself be a valid induced path (no repeats, no chords).
    for (int i = 0; i < k; i++)
        for (int j = 0; j < i; j++)
        {
            int d = __builtin_popcount((unsigned)(verts[i] ^ verts[j]));
            if (d == 0) { fprintf(stderr, "subtree_search: prefix revisits a vertex\n"); return 1; }
            if (d == 1 && i - j > 1)
            { fprintf(stderr, "subtree_search: prefix has a chord (not an induced path)\n"); return 1; }
        }

    printf("Subtree search: N=%d, prefix = %d transitions (%d vertices), transitionCounter=%d\n",
           N, ntrans, k, tc);

    Search S;

    // Pass 1: longest length within the subtree.
    S.dfs_from_partial(verts, k, tc);
    int       L     = S.maxLength;               // vertices
    long long count = S.maxSnakeCounter;
    printf("Longest snake in subtree: %d edges, count = %lld\n", L - 1, count);

    // Pass 2: emit the length-L snakes to a .bin (same format as the other tools).
    MAKEDIR("job_outputs");
    MAKEDIR("job_outputs/snakes_dfs_search");
    char path[128];
    snprintf(path, sizeof(path), "job_outputs/snakes_dfs_search/%dD_L%d_subtree.bin", N, L);
    FILE *bf = fopen(path, "wb");
    if (bf) { int hdr[2] = { N, L }; fwrite(hdr, sizeof(int), 2, bf); }
    S.targetLength = L;
    S.outFile = bf;
    S.dfs_from_partial(verts, k, tc);
    if (bf) fclose(bf);

    printf("Snakes written: %lld -> exhaustive/%s\n", S.emitCount, path);
    printf("Decode with: ./run_exhaustive.sh --dim %d --decode exhaustive/%s\n", N, path);
    return 0;
}
