/* snake_io.c - Persist a found snake to seeds/ and snakes/. See snake_io.h. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L  /* mkdir / stat under -std=c11 (POSIX) */
#endif

#include "snake_io.h"
#include "transitions.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>            /* stat / struct stat (POSIX and MSVC) */
#ifdef _WIN32
#include <direct.h>             /* _mkdir (MSVC / MinGW) */
#endif

/* Create a directory, ignoring "already exists". */
static void ensure_dir(const char *path)
{
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

/* True if a filesystem entry exists at `path`. */
static bool path_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

bool save_snake_result(const int *transitions, size_t len, int dimension)
{
    if (transitions == NULL || len == 0) {
        return false;
    }

    ensure_dir("seeds");
    ensure_dir("snakes");

    /* Find a shared title that clobbers neither the seed nor the snake file. */
    char seed_path[512];
    char snake_path[512];
    for (int k = 1;; k++) {
        if (k == 1) {
            snprintf(seed_path, sizeof(seed_path),
                     "seeds/%dD_L%zu.txt", dimension, len);
            snprintf(snake_path, sizeof(snake_path),
                     "snakes/%dD_L%zu.txt", dimension, len);
        } else {
            snprintf(seed_path, sizeof(seed_path),
                     "seeds/%dD_L%zu_%d.txt", dimension, len, k);
            snprintf(snake_path, sizeof(snake_path),
                     "snakes/%dD_L%zu_%d.txt", dimension, len, k);
        }
        if (!path_exists(seed_path) && !path_exists(snake_path)) {
            break;
        }
    }

    /* seeds/ : bare transition integers only (reloadable as an extend_snake seed). */
    FILE *fs = fopen(seed_path, "w");
    if (fs == NULL) {
        perror(seed_path);
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        fprintf(fs, "%d%s", transitions[i], (i + 1 < len) ? " " : "\n");
    }
    fclose(fs);

    /* snakes/ : human-readable record ending in the vertex sequence. */
    FILE *fk = fopen(snake_path, "w");
    if (fk == NULL) {
        perror(snake_path);
        return false;
    }
    fprintf(fk, "# %dD snake, length %zu edges (%zu vertices), heuristic pruned BFS\n",
            dimension, len, len + 1);
    fprintf(fk, "# transitions: ");
    for (size_t i = 0; i < len; i++) {
        fprintf(fk, "%d%s", transitions[i], (i + 1 < len) ? " " : "\n");
    }

    long *vertices = (long *)malloc((len + 1) * sizeof(long));
    if (vertices != NULL) {
        long n = transition_to_vertex(transitions, len, dimension, 0, vertices);
        for (long i = 0; i < n; i++) {
            fprintf(fk, "%ld%s", vertices[i], (i + 1 < n) ? " " : "\n");
        }
        free(vertices);
    }
    fclose(fk);

    printf("Saved snake -> %s\n", snake_path);
    printf("Saved seed  -> %s\n", seed_path);
    return true;
}
