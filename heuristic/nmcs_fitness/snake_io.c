/* snake_io.c - Persist a found snake to seeds/ and snakes/. See snake_io.h. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L  /* mkdir / stat under -std=c11 (POSIX) */
#endif

#include "snake_io.h"
#include "transitions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>             /* strrchr / memcpy */
#include <sys/stat.h>            /* stat / struct stat (POSIX and MSVC) */
#ifdef _WIN32
#include <direct.h>             /* _mkdir (MSVC / MinGW) */
#endif

/* Directory holding the seeds/ and snakes/ folders. Empty means "current
 * working directory" (the historical behaviour); snake_io_set_base() points it
 * at the track root derived from argv[0]. Kept comfortably under the 512-byte
 * path buffers used below. */
static char g_base_dir[384] = "";

void snake_io_set_base(const char *argv0)
{
    if (argv0 == NULL) {
        return;
    }
    /* The binary lives in .../heuristic/pruned_bfs_search/<name>, so its own
     * directory is the pruned_bfs_search folder. Strip the trailing "/<name>"
     * to get that directory; the seeds/ and snakes/ paths built below then use
     * "<dir>/.." so output lands in the parent (heuristic/). Accept either
     * slash so a Windows-style argv[0] also resolves. */
    const char *fwd = strrchr(argv0, '/');
    const char *back = strrchr(argv0, '\\');
    const char *slash = (fwd > back) ? fwd : back;
    if (slash == NULL) {
        return;  /* no directory component: keep the CWD-relative fallback */
    }
    size_t dir_len = (size_t)(slash - argv0);
    if (dir_len == 0 || dir_len >= sizeof(g_base_dir)) {
        return;  /* degenerate or too long to store safely */
    }
    memcpy(g_base_dir, argv0, dir_len);
    g_base_dir[dir_len] = '\0';
}

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

    /* Resolve the seeds/ and snakes/ folders. With a base set from argv[0] the
       binary's directory is pruned_bfs_search/, so "<base>/../seeds" lands in
       the track root (heuristic/); without one, fall back to CWD-relative. */
    char seeds_dir[448];
    char snakes_dir[448];
    if (g_base_dir[0] != '\0') {
        snprintf(seeds_dir, sizeof(seeds_dir), "%s/../seeds", g_base_dir);
        snprintf(snakes_dir, sizeof(snakes_dir), "%s/../snakes", g_base_dir);
    } else {
        snprintf(seeds_dir, sizeof(seeds_dir), "seeds");
        snprintf(snakes_dir, sizeof(snakes_dir), "snakes");
    }
    ensure_dir(seeds_dir);
    ensure_dir(snakes_dir);

    /* Find a shared title that clobbers neither the seed nor the snake file.
       A discoverer's surname (dim15_len10375_bernatonis.txt) is appended by hand,
       never here. */
    char seed_path[512];
    char snake_path[512];
    for (int k = 1;; k++) {
        if (k == 1) {
            snprintf(seed_path, sizeof(seed_path),
                     "%s/dim%d_len%zu.txt", seeds_dir, dimension, len);
            snprintf(snake_path, sizeof(snake_path),
                     "%s/dim%d_len%zu.txt", snakes_dir, dimension, len);
        } else {
            snprintf(seed_path, sizeof(seed_path),
                     "%s/dim%d_len%zu_%d.txt", seeds_dir, dimension, len, k);
            snprintf(snake_path, sizeof(snake_path),
                     "%s/dim%d_len%zu_%d.txt", snakes_dir, dimension, len, k);
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
