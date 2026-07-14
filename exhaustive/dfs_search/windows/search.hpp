/* search.hpp — the DFS search engine (passes 1 and 2), decoupled from generation.
 *
 * A worker owns one Search context: all of its own state (visited / forbidden
 * bitsets, blocked counters, the snake path, counters). Because the state is in
 * the object rather than in file-static globals, this lives in its own
 * translation unit and never collides with the generator's state.
 *
 * The engine is v13's: A1 incremental adjacency (blocked[] + forbidden bitset),
 * availCount branch-and-bound, and the two-pass emit-all hook (targetLength):
 *   pass 1 (targetLength < 0): find the longest length L, count maxima.
 *   pass 2 (targetLength == L): re-run a hit prefix and stream every length-L
 *                               snake to outFile.
 */
#pragma once
#include "config.hpp"
#include <bitset>
#include <cstdint>
#include <cstdio>

struct Search {
    std::bitset<VERTICES> visited;    /* v is in the current snake */
    std::bitset<VERTICES> forbidden;  /* adding v would chord (blocked[v] >= 2) */
    uint8_t blocked[VERTICES];        /* # snake neighbours of v (max ~N) */
    int     snake[MAX_LENGTH];

    int       availCount     = 0;
    int       maxLength       = 0;
    int       maxSnakeCounter = 0;
    long long vertexCounter   = 0;

    int       targetLength = -1;      /* <0 pass 1, ==L pass 2 */
    FILE     *outFile      = nullptr;
    long long emitCount    = 0;

    void   place(int vertex, int length);
    void   unplace(int vertex);
    void   emitSnake(int length);
    void   dfs(int vertex, int length, int transitionCounter);
    void   dfs_from_prefix(const int *vertices, int transitionCounter);

    double knuth_probe(int vertex, int length, int transitionCounter);
    double knuth_estimate_prefix(const int *vertices, int transitionCounter, int probes);
};
