/* parallel_extend.c - Parallel seeded extension of a snake into a higher dim.
 *
 * This is extend_snake.c (seed an existing snake and grow it one dimension up)
 * with parallel_search.c's OpenMP per-level expansion grafted in. The algorithm
 * is identical to extend_snake - same seeds, same fitness pruning, same beam -
 * the only difference is that each BFS level's node expansion is distributed
 * across `num_workers` threads (each owns a private child list; the merge and
 * best-snake update happen in a critical section), exactly as parallel_search
 * does for the from-scratch beam.
 *
 * Like both parents it is self-contained: it carries its own copy of the beam
 * machinery (NodeList + fitness pruning + level loop) so the other .c files are
 * left untouched and there is no duplicate symbol. It calls
 * snake_node_can_extend directly rather than redefining is_valid_extension.
 *
 * With no OpenMP support it still builds and runs correctly as a single thread.
 * Timing uses omp_get_wtime() (true wall-clock) rather than clock(), which sums
 * CPU time across threads.
 *
 * Seed sources (mix freely; each path may be a file or a directory), identical
 * to extend_snake:
 *   - text file  : space/newline-separated transition integers.
 *   - .bin file  : the exhaustive solvers' emit format (all records loaded).
 *   - directory  : every *.bin inside it is loaded.
 * With --both-ends each seed is also injected as a canonicalised reversal.
 *
 * Usage:
 *   ./parallel_extend <target_dimension> [memory_limit_gb] [num_workers]
 *                     [--both-ends] [seed ...]
 *   (defaults: memory_limit_gb = 18.0, num_workers = 10, seed = extend_input.txt)
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
#ifdef _OPENMP
#include <omp.h>
#endif
#ifdef _WIN32
#include <windows.h>           /* FindFirstFile/FindNextFile, GetFileAttributes */
#else
#include <sys/stat.h>          /* stat / S_ISDIR */
#include <dirent.h>            /* opendir / readdir */
#endif

#define MAX_DIM 64  /* dimension relabel map; N is far below this in practice */

/* True wall-clock seconds (OpenMP) or CPU seconds (fallback). */
static double now_seconds(void)
{
#ifdef _OPENMP
    return omp_get_wtime();
#else
    return (double)clock() / CLOCKS_PER_SEC;
#endif
}

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

/* ---- Seeds (identical to extend_snake.c) ------------------------------ */

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
 * canonical: first transition 0, new dimensions introduced in increasing order. */
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

/* Produce the canonicalised reversal of a seed into `out` (capacity >= len). */
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

/* Validate, canonicalise, and add one raw seed (plus its reversal if both_ends). */
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

/* ---- Seed input readers (identical to extend_snake.c) ----------------- */

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

/* ---- Worker: expand a chunk of the current level (from parallel_search.c) - */

/* Expand nodes[start, end) into `out` (a private, per-thread child list) and
 * track the longest child seen in this chunk. On return *have_best / *best hold
 * an independent copy of the longest child (the caller owns and frees it).
 *
 * `worker_budget_gb` is this worker's fair share of the memory limit (the global
 * limit / thread count). The buffer is pruned to that share whenever it
 * overshoots ~1.25x it, so the N private buffers do not collectively blow past
 * the limit, and each expanded parent is freed immediately so `current` shrinks
 * as the workers build. `current` is therefore mutated (its nodes freed); this is
 * safe because chunks are disjoint and snake_node_free is idempotent, so the
 * caller's end-of-level nodelist_free(&current_level) still works. */
static void expand_chunk(NodeList *current, size_t start, size_t end,
                         NodeList *out, bool *have_best, SnakeNode *best,
                         int *legal_dims, double worker_budget_gb)
{
    *have_best = false;
    size_t best_length = 0;
    size_t worker_cap = 0;   /* 0 = not yet sized (needs a sample child) */

    for (size_t i = start; i < end; i++) {
        const SnakeNode *node = &current->nodes[i];

        long n_legal = get_legal_next_dimensions(node->transition_sequence,
                                                 node->length, legal_dims);

        for (long k = 0; k < n_legal; k++) {
            int dim = legal_dims[k];

            if (snake_node_can_extend(node, dim)) {
                SnakeNode child;
                if (!snake_node_create_child(node, dim, &child)) {
                    continue;
                }
                if (!nodelist_push(out, child)) {
                    snake_node_free(&child);
                    continue;
                }

                /* Update this worker's local best snake. */
                const SnakeNode *pushed = &out->nodes[out->count - 1];
                size_t child_length = snake_node_get_length(pushed);
                if (child_length > best_length) {
                    best_length = child_length;
                    if (*have_best) {
                        snake_node_free(best);
                    }
                    if (snake_node_init(best, pushed->transition_sequence,
                                        pushed->length, pushed->dimension)) {
                        *have_best = true;
                    } else {
                        *have_best = false;
                    }
                }
            }
        }

        /* Parent i is fully expanded. Chunks are disjoint, so this index is ours
         * alone: free it now so current_level shrinks as the workers build. */
        snake_node_free(&current->nodes[i]);

        /* Per-worker soft cap: keep this private buffer near its fair share of
         * the budget. NOTE: capping per worker (not globally) means a chunk with
         * a disproportionate share of high-fitness nodes may drop some a single
         * global prune would keep, so the parallel beam is approximately, not
         * exactly, the serial one. */
        if (out->count > 0) {
            if (worker_cap == 0) {
                size_t bpn = estimate_node_size(&out->nodes[0]);
                size_t mn = (size_t)((worker_budget_gb * 1024.0 * 1024.0 *
                                      1024.0) / bpn);
                if (mn == 0) mn = 1;
                worker_cap = mn + mn / 4;   /* 1.25x */
            }
            if (out->count > worker_cap) {
                prune_by_fitness(out, worker_budget_gb);
            }
        }
    }
}

/* ---- Parallel seeded beam search -------------------------------------- */

/* Run the pruned BFS in dimension `dimension`, seeded with every snake in
 * `seeds` (instead of the empty root), expanding each level in parallel across
 * `num_workers` threads. Approximately equal in result to extend_snake's
 * extend_search (each worker soft-caps its buffer to its share of the memory
 * limit, so a global prune and these per-worker prunes can differ); only the
 * per-level node expansion is distributed. On success writes the longest snake
 * found to *out (caller frees) and returns true. */
static bool parallel_extend_search(int dimension, const Seed *seeds,
                                   size_t num_seeds, double memory_limit_gb,
                                   int num_workers, bool verbose, SnakeNode *out)
{
#ifndef _OPENMP
    (void)num_workers;  /* num_threads() pragma is a no-op without OpenMP */
#endif
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
    double start_time = now_seconds();
    long total_nodes_explored = 0;

    while (current_level.count > 0) {
        double level_start_time = now_seconds();
        NodeList next_level;
        nodelist_init(&next_level);

        /* Distribute the current level across workers, each expanding its own
         * chunk into a private list, then merge under a critical section. */
        #pragma omp parallel num_threads(num_workers)
        {
#ifdef _OPENMP
            int tid = omp_get_thread_num();
            int nthreads = omp_get_num_threads();
#else
            int tid = 0;
            int nthreads = 1;
#endif
            size_t total = current_level.count;
            size_t chunk = (total + (size_t)nthreads - 1) / (size_t)nthreads;
            size_t start = (size_t)tid * chunk;
            size_t end = start + chunk;
            if (start > total) start = total;
            if (end > total) end = total;

            int *legal_dims =
                (int *)malloc((size_t)(dimension + 1) * sizeof(int));
            NodeList local_next;
            nodelist_init(&local_next);
            bool local_have_best = false;
            SnakeNode local_best;
            long local_nodes = 0;

            if (legal_dims != NULL) {
                /* Each worker gets an equal share of the memory limit so the
                 * private buffers sum to roughly the limit, not N x it. */
                double worker_budget_gb =
                    memory_limit_gb / (double)(nthreads > 0 ? nthreads : 1);
                expand_chunk(&current_level, start, end, &local_next,
                             &local_have_best, &local_best, legal_dims,
                             worker_budget_gb);
                local_nodes = (long)local_next.count;
                free(legal_dims);
            }

            #pragma omp critical
            {
                for (size_t j = 0; j < local_next.count; j++) {
                    if (!nodelist_push(&next_level, local_next.nodes[j])) {
                        snake_node_free(&local_next.nodes[j]);
                    }
                }
                local_next.count = 0;   /* nodes moved into next_level */
                total_nodes_explored += local_nodes;

                if (local_have_best) {
                    if (snake_node_get_length(&local_best) > max_length) {
                        max_length = snake_node_get_length(&local_best);
                        if (have_best) {
                            snake_node_free(&best_snake);
                        }
                        best_snake = local_best;   /* transfer ownership */
                        have_best = true;
                        local_have_best = false;
                        if (verbose) {
                            printf("Level %d: New best length %zu\n",
                                   level_count + 1, max_length);
                        }
                    }
                }
            }

            if (local_have_best) {
                snake_node_free(&local_best);
            }
            nodelist_free(&local_next);
        }

        /* Final exact prune of the merged level. The per-worker soft caps in
         * expand_chunk already keep the workers' buffers (and hence this merge)
         * near the budget; this trims the union to exactly the limit. */
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

        double level_elapsed = now_seconds() - level_start_time;
        if (verbose) {
            printf("Level %d: %zu nodes, best length: %zu, time: %.3fs\n",
                   level_count, current_level.count, max_length, level_elapsed);
        }

        if (current_level.count == 0) {
            break;
        }
    }

    double total_time = now_seconds() - start_time;
    if (verbose) {
        printf("Search completed: %.2fs, %d levels, %ld nodes explored\n",
               total_time, level_count, total_nodes_explored);
    }

    nodelist_free(&current_level);

    if (have_best) {
        *out = best_snake;  /* transfer ownership to caller */
        return true;
    }
    return false;
}

/* ---- Driver ----------------------------------------------------------- */

/* True iff the whole string parses as a floating-point number (so a .bin file
 * name beginning with a digit is NOT mistaken for a numeric argument). */
static bool is_number(const char *s)
{
    char *end;
    strtod(s, &end);
    return *s != '\0' && *end == '\0';
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <target_dimension> [memory_limit_gb] [num_workers] "
        "[--both-ends] [seed ...]\n"
        "  memory_limit_gb : approximate PEAK RSS cap (default 18.0). The beam is\n"
        "                    pruned during each level's build (per worker, at\n"
        "                    limit/num_workers each), so peak stays near this.\n"
        "  num_workers     : OpenMP threads for per-level expansion (default 10).\n"
        "  seed            : text file (transition integers), .bin file (all\n"
        "                    records loaded), or a directory of .bin files.\n"
        "                    Multiple seeds allowed; default extend_input.txt.\n"
        "  --both-ends     : also grow each seed from its other endpoint.\n",
        prog);
}

int main(int argc, char **argv)
{
    snake_io_set_base(argv[0]);  /* anchor seeds/ and snakes/ at the track root */
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
    int num_workers = 10;
    int numbers_seen = 0;         /* 1st bare number = memory, 2nd = workers */
    bool both_ends = false;
    const char **paths = (const char **)malloc((size_t)argc * sizeof(char *));
    int npaths = 0;

    for (int ai = 2; ai < argc; ai++) {
        const char *a = argv[ai];
        if (strcmp(a, "--both-ends") == 0) { both_ends = true; continue; }
        if (strcmp(a, "--help") == 0) { usage(argv[0]); free(paths); return 0; }
        /* Bare numbers, before any seed path, are memory then workers. */
        if (npaths == 0 && numbers_seen < 2 && is_number(a)) {
            if (numbers_seen == 0) memory_limit_gb = atof(a);
            else                   num_workers = atoi(a);
            numbers_seen++;
            continue;
        }
        paths[npaths++] = a;
    }
    if (npaths == 0) paths[npaths++] = "extend_input.txt";
    if (num_workers < 1) num_workers = 1;

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

    printf("Snake-in-the-box parallel seeded extension\n");
    printf("Dimension: %d, memory limit: %.1f GB, workers: %d, both-ends: %s\n",
           dimension, memory_limit_gb, num_workers, both_ends ? "yes" : "no");
#ifndef _OPENMP
    printf("(built without OpenMP: running single-threaded)\n");
#endif
    printf("Seeds injected: %zu\n\n", seeds.count);

    SnakeNode best;
    bool ok = parallel_extend_search(dimension, seeds.items, seeds.count,
                                     memory_limit_gb, num_workers, true, &best);
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
