/* extend_snake.c - Extend existing snake(s) into a higher dimension.
 *
 * The normal beam (bfs_pruned.c) always starts from the empty snake at the
 * origin. This tool instead *seeds* the same pruned breadth-first search with
 * already-found snake(s) and continues them in a larger hypercube. A snake's
 * transition sequence is still a valid snake when embedded in Q_D for any
 * D >= its own dimension, so it makes a legitimate search seed ("priming"): the
 * search is then free to grow it into the newly available dimension(s).
 *
 * NOTE: this is a HEURISTIC LOWER-BOUND tool. It only explores snakes that
 * begin with a supplied seed, so it can never prove a maximum and may not beat
 * the from-scratch beam. Its value is as a bridge: feed the exhaustive solvers'
 * emitted longest snakes straight in and try to grow them one dimension up.
 *
 * Seed sources (mix freely; each path may be a file or a directory):
 *   - text file  : space/newline-separated transition integers (bit positions),
 *                  exactly as the beam's main.c prints after "Transitions:".
 *   - .bin file  : the exhaustive solvers' emit format - header [N, L] (two
 *                  ints), then one or more records of L-1 bytes, each byte the
 *                  flipped dimension. Every record in the file becomes a seed
 *                  (so one rank file = all its longest snakes = free multi-seed).
 *   - directory  : every *.bin inside it is loaded.
 * Every seed is canonicalised (dimensions relabelled by first appearance) so the
 * canonical next-dimension enumeration stays correct.
 *
 * With --both-ends each seed is injected twice: as-is (grows its head) and as a
 * canonicalised reversal (grows the other endpoint), since the beam only ever
 * grows the head.
 *
 * This file is self-contained: it carries its own copy of the beam machinery
 * (NodeList + fitness pruning + level loop) from bfs_pruned.c, seeded from the
 * prefix rather than the root, so bfs_pruned.c is left untouched. To avoid a
 * duplicate symbol it does not redefine is_valid_extension; it calls
 * snake_node_can_extend directly.
 *
 * Usage:
 *   ./extend_snake <target_dimension> [memory_limit_gb] [--both-ends] [seed ...]
 *   (defaults: memory_limit_gb = 18.0, seed = extend_input.txt)
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L  /* opendir/readdir/stat under -std=c11 on glibc */
#endif

#include "snake_node.h"
#include "canonical.h"
#include "transitions.h"
#include "validation.h"
#include "snake_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>           /* FindFirstFile/FindNextFile, GetFileAttributes */
#else
#include <sys/stat.h>          /* stat / S_ISDIR */
#include <dirent.h>            /* opendir / readdir */
#endif

#define MAX_DIM 64  /* dimension relabel map; N is far below this in practice */

/* ---- Growable list of SnakeNode (one search level) -------------------- */
/* (Copied from bfs_pruned.c; kept static so there is no link clash.)       */

typedef struct {
    SnakeNode *nodes;
    size_t count;
    size_t capacity;
} NodeList;

static void nodelist_init(NodeList *list)
{
    list->nodes = NULL;
    list->count = 0;
    list->capacity = 0;
}

/* Append node by value (ownership transfers into the list). */
static bool nodelist_push(NodeList *list, SnakeNode node)
{
    if (list->count == list->capacity) {
        size_t new_cap = (list->capacity == 0) ? 16 : list->capacity * 2;
        SnakeNode *grown =
            (SnakeNode *)realloc(list->nodes, new_cap * sizeof(SnakeNode));
        if (grown == NULL) {
            return false;
        }
        list->nodes = grown;
        list->capacity = new_cap;
    }
    list->nodes[list->count++] = node;
    return true;
}

/* Free every node still held by the list, then the backing array. */
static void nodelist_free(NodeList *list)
{
    for (size_t i = 0; i < list->count; i++) {
        snake_node_free(&list->nodes[i]);
    }
    free(list->nodes);
    nodelist_init(list);
}

/* ---- Memory estimation and pruning ------------------------------------ */

/* Estimate memory size of a node in bytes (transition seq + bitmap + overhead). */
static size_t estimate_node_size(const SnakeNode *node)
{
    size_t transition_size = node->length * sizeof(int);
    size_t bitmap_size = node->vertices_bitmap.num_words * 8; /* 8 bytes/word */
    size_t object_overhead = 200 + sizeof(SnakeNode);
    return transition_size + bitmap_size + object_overhead;
}

/* Estimate memory usage for a list of nodes, in gigabytes. */
static double estimate_memory_usage(const NodeList *list)
{
    if (list->count == 0) {
        return 0.0;
    }
    size_t bytes_per_node = estimate_node_size(&list->nodes[0]);
    double total_bytes = (double)list->count * (double)bytes_per_node;
    return total_bytes / (1024.0 * 1024.0 * 1024.0); /* Convert to GB */
}

/* qsort comparator: fitness descending. */
static int compare_fitness_desc(const void *a, const void *b)
{
    const SnakeNode *na = (const SnakeNode *)a;
    const SnakeNode *nb = (const SnakeNode *)b;
    if (na->fitness < nb->fitness) return 1;
    if (na->fitness > nb->fitness) return -1;
    return 0;
}

/* Prune nodes by fitness (unmarked vertex count) to fit the memory limit. */
static void prune_by_fitness(NodeList *list, double memory_limit_gb)
{
    if (list->count == 0) {
        return;
    }

    size_t bytes_per_node = estimate_node_size(&list->nodes[0]);
    size_t max_nodes =
        (size_t)((memory_limit_gb * 1024.0 * 1024.0 * 1024.0) / bytes_per_node);

    if (list->count <= max_nodes) {
        return;
    }

    qsort(list->nodes, list->count, sizeof(SnakeNode), compare_fitness_desc);

    for (size_t i = max_nodes; i < list->count; i++) {
        snake_node_free(&list->nodes[i]);
    }
    list->count = max_nodes;
}

/* ---- Seeds ------------------------------------------------------------ */

typedef struct {
    int   *trans;   /* owned transition array */
    size_t len;
} Seed;

typedef struct {
    Seed  *items;
    size_t count;
    size_t capacity;
} SeedList;

static void seedlist_init(SeedList *sl)
{
    sl->items = NULL;
    sl->count = 0;
    sl->capacity = 0;
}

static void seedlist_free(SeedList *sl)
{
    for (size_t i = 0; i < sl->count; i++) {
        free(sl->items[i].trans);
    }
    free(sl->items);
    seedlist_init(sl);
}

/* Copy `len` transitions into the list (takes its own allocation). */
static bool seedlist_add(SeedList *sl, const int *trans, size_t len)
{
    if (sl->count == sl->capacity) {
        size_t new_cap = (sl->capacity == 0) ? 8 : sl->capacity * 2;
        Seed *grown = (Seed *)realloc(sl->items, new_cap * sizeof(Seed));
        if (grown == NULL) return false;
        sl->items = grown;
        sl->capacity = new_cap;
    }
    int *copy = (int *)malloc((len ? len : 1) * sizeof(int));
    if (copy == NULL) return false;
    memcpy(copy, trans, len * sizeof(int));
    sl->items[sl->count].trans = copy;
    sl->items[sl->count].len = len;
    sl->count++;
    return true;
}

/* Relabel dimensions by order of first appearance (in place) so the sequence is
 * canonical: first transition 0, new dimensions introduced in increasing order.
 * Idempotent for already-canonical seeds (e.g. from the solvers/beam). Required
 * because get_legal_next_dimensions only offers already-used dims + max+1, so a
 * non-canonical seed would silently lose legal moves. */
static void canonicalize_transitions(int *seq, size_t len)
{
    int map[MAX_DIM];
    for (int i = 0; i < MAX_DIM; i++) map[i] = -1;
    int next = 0;
    for (size_t i = 0; i < len; i++) {
        int d = seq[i];
        if (d < 0 || d >= MAX_DIM) continue;  /* shouldn't happen post-validate */
        if (map[d] == -1) map[d] = next++;
        seq[i] = map[d];
    }
}

/* Produce the canonicalised reversal of a seed into `out` (capacity >= len).
 * Reverse the vertex path, re-anchor at the origin, re-derive transitions, then
 * canonicalise. Returns the length, or -1 on error. */
static long reverse_seed(const int *seq, size_t len, int dimension, int *out)
{
    long *verts = (long *)malloc((len + 1) * sizeof(long));
    if (verts == NULL) return -1;
    long nv = transition_to_vertex(seq, len, dimension, 0, verts);
    if (nv < 2) { free(verts); return -1; }

    for (long i = 0, j = nv - 1; i < j; i++, j--) {
        long t = verts[i]; verts[i] = verts[j]; verts[j] = t;
    }
    long anchor = verts[0];
    for (long i = 0; i < nv; i++) verts[i] ^= anchor;

    long nt = vertex_to_transition(verts, (size_t)nv, out);
    free(verts);
    if (nt < 0) return -1;
    canonicalize_transitions(out, (size_t)nt);
    return nt;
}

/* Validate, canonicalise, and add one raw seed (plus its reversal if both_ends).
 * Skips (with a warning) any seed that isn't a valid snake in the target dim. */
static void process_seed(SeedList *sl, int *trans, size_t len, int dimension,
                         bool both_ends, const char *origin)
{
    char msg[256];
    if (len == 0) return;
    if (!validate_transition_sequence(trans, len, dimension, msg, sizeof(msg))) {
        fprintf(stderr, "Skipping invalid seed from %s: %s\n", origin, msg);
        return;
    }
    canonicalize_transitions(trans, len);
    if (!seedlist_add(sl, trans, len)) {
        fprintf(stderr, "Out of memory adding seed from %s\n", origin);
        return;
    }
    if (both_ends) {
        int *rev = (int *)malloc((len ? len : 1) * sizeof(int));
        if (rev == NULL) return;
        long rn = reverse_seed(trans, len, dimension, rev);
        if (rn > 0 &&
            validate_transition_sequence(rev, (size_t)rn, dimension, msg, sizeof(msg))) {
            seedlist_add(sl, rev, (size_t)rn);
        }
        free(rev);
    }
}

/* ---- Seed input readers ----------------------------------------------- */

static bool has_suffix(const char *s, const char *suf)
{
    size_t ls = strlen(s), lf = strlen(suf);
    return ls >= lf && strcmp(s + ls - lf, suf) == 0;
}

/* Read space/newline-separated transition integers (one seed) from a text file. */
static void load_text_file(SeedList *sl, const char *path, int dimension,
                           bool both_ends)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "Could not open seed file '%s'\n", path);
        return;
    }
    long cap = (long)1 << dimension;
    int *buf = (int *)malloc((size_t)cap * sizeof(int));
    if (buf == NULL) { fclose(f); return; }

    long count = 0;
    int value;
    bool ok = true;
    while (fscanf(f, "%d", &value) == 1) {
        if (count >= cap) {
            fprintf(stderr, "Seed '%s' too long (> %ld transitions)\n", path, cap);
            ok = false;
            break;
        }
        buf[count++] = value;
    }
    if (ok) {
        int c;
        while ((c = fgetc(f)) != EOF) {
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                fprintf(stderr, "Seed '%s' contains non-integer text\n", path);
                ok = false;
                break;
            }
        }
    }
    fclose(f);
    if (ok && count > 0) {
        process_seed(sl, buf, (size_t)count, dimension, both_ends, path);
    }
    free(buf);
}

/* Read the exhaustive solvers' .bin format: header [N, L] (two ints), then one
 * or more records of L-1 transition bytes. Every record becomes a seed. */
static void load_bin_file(SeedList *sl, const char *path, int dimension,
                          bool both_ends)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "Could not open seed file '%s'\n", path);
        return;
    }
    int hdr[2];
    if (fread(hdr, sizeof(int), 2, f) != 2) {
        fprintf(stderr, "Bad .bin header in '%s'\n", path);
        fclose(f);
        return;
    }
    int L = hdr[1];               /* vertices in each snake = edges + 1 */
    long steps = (long)L - 1;     /* transitions per record */
    if (steps <= 0) {
        fprintf(stderr, "Nonsensical snake length L=%d in '%s'\n", L, path);
        fclose(f);
        return;
    }
    unsigned char *bytes = (unsigned char *)malloc((size_t)steps);
    int *trans = (int *)malloc((size_t)steps * sizeof(int));
    if (bytes == NULL || trans == NULL) {
        free(bytes); free(trans); fclose(f);
        return;
    }
    size_t records = 0;
    while (fread(bytes, 1, (size_t)steps, f) == (size_t)steps) {
        for (long i = 0; i < steps; i++) trans[i] = (int)bytes[i];
        process_seed(sl, trans, (size_t)steps, dimension, both_ends, path);
        records++;
    }
    if (records == 0) {
        fprintf(stderr, "No snake records read from '%s'\n", path);
    }
    free(bytes);
    free(trans);
    fclose(f);
}

static void load_file(SeedList *sl, const char *path, int dimension,
                      bool both_ends)
{
    if (has_suffix(path, ".bin")) {
        load_bin_file(sl, path, dimension, both_ends);
    } else {
        load_text_file(sl, path, dimension, both_ends);
    }
}

/* True if `path` names a directory (portable). */
static bool is_directory(const char *path)
{
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

/* A seed path may be a file or a directory (all *.bin inside it are loaded). */
static void load_path(SeedList *sl, const char *path, int dimension,
                      bool both_ends)
{
    if (!is_directory(path)) {
        load_file(sl, path, dimension, both_ends);
        return;
    }
#ifdef _WIN32
    char pattern[4096];
    snprintf(pattern, sizeof(pattern), "%s\\*.bin", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return;  /* empty or unreadable directory: no .bin seeds */
    }
    do {
        char full[4096];
        snprintf(full, sizeof(full), "%s\\%s", path, fd.cFileName);
        load_bin_file(sl, full, dimension, both_ends);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(path);
    if (d == NULL) {
        fprintf(stderr, "Could not open directory '%s'\n", path);
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!has_suffix(e->d_name, ".bin")) continue;
        char full[4096];
        snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
        load_bin_file(sl, full, dimension, both_ends);
    }
    closedir(d);
#endif
}

/* ---- Seeded beam search ----------------------------------------------- */

/* Run the pruned BFS in dimension `dimension`, seeded with every snake in
 * `seeds` (instead of the empty root). Identical to pruned_bfs_search except for
 * the seeds and the initial best (the longest seed). On success writes the
 * longest snake found to *out (caller frees) and returns true. */
static bool extend_search(int dimension, const Seed *seeds, size_t num_seeds,
                          double memory_limit_gb, bool verbose, SnakeNode *out)
{
    if (num_seeds == 0) return false;

    NodeList current_level;
    nodelist_init(&current_level);

    /* Seed the frontier with every supplied snake (not an empty root). */
    size_t longest = 0;
    for (size_t i = 0; i < num_seeds; i++) {
        SnakeNode node;
        if (!snake_node_init(&node, seeds[i].trans, seeds[i].len, dimension)) {
            nodelist_free(&current_level);
            return false;
        }
        if (!nodelist_push(&current_level, node)) {
            snake_node_free(&node);
            nodelist_free(&current_level);
            return false;
        }
        if (seeds[i].len > seeds[longest].len) longest = i;
    }

    /* The longest seed is the initial incumbent. */
    bool have_best = true;
    SnakeNode best_snake;
    size_t max_length = seeds[longest].len;
    if (!snake_node_init(&best_snake, seeds[longest].trans, seeds[longest].len,
                         dimension)) {
        nodelist_free(&current_level);
        return false;
    }

    int level_count = 0;
    clock_t start_time = clock();
    long total_nodes_explored = 0;

    /* Scratch buffer for legal next dimensions (at most dimension + 1). */
    int *legal_dims = (int *)malloc((size_t)(dimension + 1) * sizeof(int));
    if (legal_dims == NULL) {
        nodelist_free(&current_level);
        snake_node_free(&best_snake);
        return false;
    }

    while (current_level.count > 0) {
        clock_t level_start_time = clock();
        NodeList next_level;
        nodelist_init(&next_level);

        /* Peak-memory guard. Without an in-build cap the whole next level is
         * materialised (branching_factor x the beam) and coexists with
         * current_level, so peak RSS runs to several x memory_limit_gb even
         * though the end-of-level prune eventually cuts it back. soft_cap is the
         * count at which we prune mid-build; 0 means "not yet computed" (we need
         * one sample node to size it). */
        size_t soft_cap = 0;

        /* Generate all children for the current level. */
        for (size_t i = 0; i < current_level.count; i++) {
            SnakeNode *node = &current_level.nodes[i];

            long n_legal = get_legal_next_dimensions(node->transition_sequence,
                                                     node->length, legal_dims);

            for (long k = 0; k < n_legal; k++) {
                int dim = legal_dims[k];

                if (snake_node_can_extend(node, dim)) {
                    SnakeNode child;
                    if (!snake_node_create_child(node, dim, &child)) {
                        continue;
                    }
                    if (!nodelist_push(&next_level, child)) {
                        snake_node_free(&child);
                        continue;
                    }
                    total_nodes_explored += 1;

                    size_t child_length =
                        snake_node_get_length(&next_level.nodes[next_level.count - 1]);
                    if (child_length > max_length) {
                        max_length = child_length;
                        if (have_best) {
                            snake_node_free(&best_snake);
                        }
                        const SnakeNode *src = &next_level.nodes[next_level.count - 1];
                        if (snake_node_init(&best_snake, src->transition_sequence,
                                            src->length, src->dimension)) {
                            have_best = true;
                        } else {
                            have_best = false;
                        }
                        if (verbose) {
                            printf("Level %d: New best length %zu\n",
                                   level_count + 1, max_length);
                        }
                    }
                }
            }

            /* Parent i is fully expanded; free it now so current_level shrinks
             * as next_level grows (the two no longer both sit at full size at
             * peak). snake_node_free NULLs its pointers and is idempotent, so
             * the end-of-level nodelist_free(&current_level) stays safe. */
            snake_node_free(&current_level.nodes[i]);

            /* Soft-cap incremental prune: once next_level overshoots ~1.25x the
             * node budget, cut it back to the budget mid-build. Bounds its peak
             * to ~1.25x memory_limit_gb instead of branching_factor x it. Uses
             * the same fitness target as the end-of-level prune, so the surviving
             * set is unchanged: a node's fitness is fixed at creation, so a node
             * outside the top max_nodes seen so far can never re-enter it. */
            if (soft_cap == 0 && next_level.count > 0) {
                size_t bpn = estimate_node_size(&next_level.nodes[0]);
                size_t max_nodes =
                    (size_t)((memory_limit_gb * 1024.0 * 1024.0 * 1024.0) / bpn);
                if (max_nodes == 0) max_nodes = 1;
                soft_cap = max_nodes + max_nodes / 4;  /* 1.25x */
            }
            if (soft_cap != 0 && next_level.count > soft_cap) {
                prune_by_fitness(&next_level, memory_limit_gb);
            }
        }

        /* Final exact prune if still over the limit (usually a no-op now — the
         * incremental soft-cap prune above keeps next_level near the budget
         * throughout the build). */
        if (estimate_memory_usage(&next_level) > memory_limit_gb) {
            if (verbose) {
                printf("Level %d: Pruning %zu nodes to fit memory limit\n",
                       level_count + 1, next_level.count);
            }
            prune_by_fitness(&next_level, memory_limit_gb);
        }

        nodelist_free(&current_level);
        current_level = next_level;
        level_count += 1;

        double level_elapsed =
            (double)(clock() - level_start_time) / CLOCKS_PER_SEC;

        if (verbose) {
            printf("Level %d: %zu nodes, best length: %zu, time: %.3fs\n",
                   level_count, current_level.count, max_length, level_elapsed);
        }

        if (current_level.count == 0) {
            break;
        }
    }

    double total_time = (double)(clock() - start_time) / CLOCKS_PER_SEC;

    if (verbose) {
        printf("Search completed: %.2fs, %d levels, %ld nodes explored\n",
               total_time, level_count, total_nodes_explored);
    }

    free(legal_dims);
    nodelist_free(&current_level);

    if (have_best) {
        *out = best_snake;  /* transfer ownership to caller */
        return true;
    }
    return false;
}

/* ---- Driver ----------------------------------------------------------- */

/* True iff the whole string parses as a floating-point number (so a .bin file
 * name beginning with a digit, e.g. "5D_L14_rank1.bin", is NOT mistaken for the
 * memory-limit argument). */
static bool is_number(const char *s)
{
    char *end;
    strtod(s, &end);
    return *s != '\0' && *end == '\0';
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <target_dimension> [memory_limit_gb] [--both-ends] [seed ...]\n"
        "  memory_limit_gb : approximate PEAK RSS cap for the beam (default 18.0).\n"
        "         The beam is pruned during each level's build, so peak stays near\n"
        "         this value (allow ~1.25x slack), not several x it.\n"
        "  seed : a text file (transition integers), a .bin file (exhaustive\n"
        "         emit format, all records loaded), or a directory of .bin files.\n"
        "         Multiple seeds allowed; default is extend_input.txt.\n"
        "  --both-ends : also grow each seed from its other endpoint.\n",
        prog);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    int dimension = atoi(argv[1]);
    if (dimension < 1 || dimension >= MAX_DIM) {
        fprintf(stderr, "Target dimension must be in [1, %d), got %d\n",
                MAX_DIM, dimension);
        return 1;
    }

    double memory_limit_gb = 18.0;
    bool mem_set = false;
    bool both_ends = false;
    const char **paths = (const char **)malloc((size_t)argc * sizeof(char *));
    int npaths = 0;

    for (int ai = 2; ai < argc; ai++) {
        const char *a = argv[ai];
        if (strcmp(a, "--both-ends") == 0) { both_ends = true; continue; }
        if (strcmp(a, "--help") == 0) { usage(argv[0]); free(paths); return 0; }
        /* first bare number, before any seed path, is the memory limit */
        if (!mem_set && npaths == 0 && is_number(a)) {
            memory_limit_gb = atof(a);
            mem_set = true;
            continue;
        }
        paths[npaths++] = a;
    }
    if (npaths == 0) paths[npaths++] = "extend_input.txt";

    SeedList seeds;
    seedlist_init(&seeds);
    for (int i = 0; i < npaths; i++) {
        load_path(&seeds, paths[i], dimension, both_ends);
    }
    free(paths);

    if (seeds.count == 0) {
        fprintf(stderr, "No valid seeds loaded.\n");
        seedlist_free(&seeds);
        return 1;
    }

    printf("Snake-in-the-box seeded beam search (extension)\n");
    printf("Dimension: %d, memory limit: %.1f GB, both-ends: %s\n",
           dimension, memory_limit_gb, both_ends ? "yes" : "no");
    printf("Seeds injected: %zu\n\n", seeds.count);

    SnakeNode best;
    bool ok = extend_search(dimension, seeds.items, seeds.count,
                            memory_limit_gb, true, &best);
    seedlist_free(&seeds);
    if (!ok) {
        fprintf(stderr, "Search failed.\n");
        return 1;
    }

    size_t len = snake_node_get_length(&best);
    printf("\nFound snake of length %zu (fitness %ld)\n", len, best.fitness);

    printf("Transitions: ");
    for (size_t i = 0; i < len; i++) {
        printf("%d ", best.transition_sequence[i]);
    }
    printf("\n");

    char msg[256];
    long *vertices = (long *)malloc((len + 1) * sizeof(long));
    if (vertices != NULL) {
        long n = transition_to_vertex(best.transition_sequence, len, dimension,
                                      0, vertices);
        printf("Vertices:    ");
        for (long i = 0; i < n; i++) {
            printf("%ld ", vertices[i]);
        }
        printf("\n");

        bool valid = validate_snake(vertices, (size_t)n, msg, sizeof(msg));
        printf("Validation:  %s (%s)\n", valid ? "VALID" : "INVALID", msg);
        free(vertices);
    }

    save_snake_result(best.transition_sequence, len, dimension);

    snake_node_free(&best);
    return 0;
}
