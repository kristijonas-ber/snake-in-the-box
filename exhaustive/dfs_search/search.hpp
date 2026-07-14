#pragma once
#include "config.hpp"
#include <bitset>
#include <cstdint>
#include <cstdio>

struct Search {
    std::bitset<VERTICES> visited;
    std::bitset<VERTICES> forbidden;
    uint8_t blocked[VERTICES];
    int     snake[MAX_LENGTH];

    int       availCount     = 0;
    int       maxLength       = 0;
    int       maxSnakeCounter = 0;
    long long vertexCounter   = 0;

    int       targetLength = -1;
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
