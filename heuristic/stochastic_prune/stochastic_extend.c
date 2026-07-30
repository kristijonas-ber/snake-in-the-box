/* stochastic_extend.c - Extend an existing snake using EPSILON-RANDOM pruning.
 *
 * Independent sibling of ../pruned_bfs_search/extend_snake.c. Per project policy
 * (see ../../notes/search-strategy-ideas.md and ../../CLAUDE.md), the proven
 * deterministic beam search in pruned_bfs_search/ is never edited in place; this
 * is a separate implementation exploring a different pruning strategy, built from
 * its own copies of the shared utility modules (hypercube/transitions/canonical/
 * validation/snake_node/fitness/snake_io), so it shares no mutable state with the
 * original tool.
 *
 * MOTIVATION (see notes/search-strategy-ideas.md, idea #1/#2): pruned_bfs_search's
 * survivor selection is a deterministic qsort-by-fitness-descending followed by a
 * hard top-K truncation, with zero randomness anywhere. Empirically (this repo's
 * 2026-07-26 session) that plateaus at the exact same result regardless of memory
 * budget (e.g. dim9->10 gave 369-370 edges at 0.1/1/4/4 GB; dim10->11 gave exactly
 * 737 at both 1 and 4 GB) -- widening the beam under a strict top-K rule just keeps
 * more correlated near-duplicates of the same greedy choice alive, not a genuinely
 * different search. This is Meyerson et al.'s "stochastic beam search" idea
 * (credited with several dim 10-13 records in arXiv:1603.05119) generalized as a
 * tunable epsilon: keep the top (1-epsilon) fraction of the budget by fitness
 * (exploitation), and fill the remaining epsilon fraction with a uniform-random
 * sample of the REST of the candidates (exploration) instead of discarding them
 * outright. epsilon=0 reproduces pruned_bfs_search's exact behaviour; epsilon=1 is
 * Kris's original "pure random pruning" idea, kept as a selectable extreme rather
 * than the default (see notes doc for why: a single pure-random trial should do
 * worse on average than greedy, its value is as a best-of-many-trials strategy).
 *
 * Usage:
 *   ./stochastic_extend <target_dimension> [memory_limit_gb] [--epsilon E]
 *                        [--seed N] [--trials T] [seed_file]
 *   memory_limit_gb : approximate peak RSS cap for the beam (default 18.0).
 *   --epsilon E     : fraction (0..1) of the surviving beam chosen at random
 *                     instead of by fitness, each level (default 0.1).
 *   --seed N        : RNG seed (default: time-based, so trials differ).
 *   --trials T      : run the whole search T times independently (default 1),
 *                     report the best result across trials. This is the
 *                     "random restart" idea (notes doc #3) made usable without
 *                     needing N times the memory of a single wide-beam run.
 *   seed_file       : text file of transition integers (same format as
 *                     pruned_bfs_search's seeds/ txt files). Default extend_input.txt.
 */
#define _POSIX_C_SOURCE 200809L

#include "snake_node.h"
#include "canonical.h"
#include "transitions.h"
#include "validation.h"
#include "snake_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_DIM 64

/* ---- Growable list of SnakeNode (one search level) -------------------- */

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

static void nodelist_free(NodeList *list)
{
    for (size_t i = 0; i < list->count; i++) {
        snake_node_free(&list->nodes[i]);
    }
    free(list->nodes);
    nodelist_init(list);
}

/* ---- Memory estimation ------------------------------------------------- */

static size_t estimate_node_size(const SnakeNode *node)
{
    size_t transition_size = node->length * sizeof(int);
    size_t bitmap_size = node->vertices_bitmap.num_words * 8;
    size_t object_overhead = 200 + sizeof(SnakeNode);
    return transition_size + bitmap_size + object_overhead;
}

static double estimate_memory_usage(const NodeList *list)
{
    if (list->count == 0) {
        return 0.0;
    }
    size_t bytes_per_node = estimate_node_size(&list->nodes[0]);
    double total_bytes = (double)list->count * (double)bytes_per_node;
    return total_bytes / (1024.0 * 1024.0 * 1024.0);
}

static int compare_fitness_desc(const void *a, const void *b)
{
    const SnakeNode *na = (const SnakeNode *)a;
    const SnakeNode *nb = (const SnakeNode *)b;
    if (na->fitness < nb->fitness) return 1;
    if (na->fitness > nb->fitness) return -1;
    return 0;
}

/* ---- Epsilon-random pruning --------------------------------------------
 *
 * Sort descending by fitness (same as the deterministic tool). Keep the top
 * `elite` slots outright (exploitation). For the remaining `random_slots`
 * survivor slots, draw a uniform-random sample WITHOUT replacement from the
 * rest of the list (indices [elite, count)) via a partial Fisher-Yates: swap a
 * random remaining element into the next free slot, one draw per slot needed.
 * This costs O(random_slots) swaps, not O(count) -- important since count can
 * be in the millions.
 */
static void prune_stochastic(NodeList *list, double memory_limit_gb,
                             double epsilon)
{
    if (list->count == 0) {
        return;
    }

    size_t bytes_per_node = estimate_node_size(&list->nodes[0]);
    size_t max_nodes =
        (size_t)((memory_limit_gb * 1024.0 * 1024.0 * 1024.0) / bytes_per_node);
    if (max_nodes == 0) max_nodes = 1;

    if (list->count <= max_nodes) {
        return;
    }

    qsort(list->nodes, list->count, sizeof(SnakeNode), compare_fitness_desc);

    size_t elite = (size_t)((1.0 - epsilon) * (double)max_nodes);
    if (elite > max_nodes) elite = max_nodes;
    size_t random_slots = max_nodes - elite;

    /* Partial Fisher-Yates over the tail [elite, count): for each of the
     * random_slots slots right after `elite`, swap in a uniformly-random
     * element from the remaining unshuffled tail. After this loop,
     * [elite, elite+random_slots) holds a uniform random sample of the
     * original tail, and everything is contiguous with the elite prefix. */
    size_t tail_start = elite;
    size_t tail_len = list->count - elite;
    for (size_t i = 0; i < random_slots && i < tail_len; i++) {
        size_t remaining = tail_len - i;
        size_t pick = (size_t)((double)rand() / ((double)RAND_MAX + 1.0) *
                               (double)remaining);
        if (pick >= remaining) pick = remaining - 1;
        size_t a = tail_start + i;
        size_t b = tail_start + i + pick;
        SnakeNode tmp = list->nodes[a];
        list->nodes[a] = list->nodes[b];
        list->nodes[b] = tmp;
    }

    size_t keep = elite + random_slots;
    if (keep > list->count) keep = list->count;

    for (size_t i = keep; i < list->count; i++) {
        snake_node_free(&list->nodes[i]);
    }
    list->count = keep;
}

/* ---- Seed loading (single text-file seed; simplified vs. extend_snake.c,
 * which also supports .bin files and directories -- not needed here since
 * this tool is for experimenting with pruning strategy, not for bulk-loading
 * exhaustive-solver output). ------------------------------------------- */

static void canonicalize_transitions(int *seq, size_t len)
{
    int map[MAX_DIM];
    for (int i = 0; i < MAX_DIM; i++) map[i] = -1;
    int next = 0;
    for (size_t i = 0; i < len; i++) {
        int d = seq[i];
        if (d < 0 || d >= MAX_DIM) continue;
        if (map[d] == -1) map[d] = next++;
        seq[i] = map[d];
    }
}

static bool load_seed(const char *path, int dimension, int **out_trans,
                      size_t *out_len)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "Could not open seed file '%s'\n", path);
        return false;
    }
    long cap = (long)1 << dimension;
    int *buf = (int *)malloc((size_t)cap * sizeof(int));
    if (buf == NULL) { fclose(f); return false; }

    long count = 0;
    int value;
    while (fscanf(f, "%d", &value) == 1) {
        if (count >= cap) {
            fprintf(stderr, "Seed '%s' too long (> %ld transitions)\n", path, cap);
            free(buf);
            fclose(f);
            return false;
        }
        buf[count++] = value;
    }
    fclose(f);

    if (count == 0) {
        fprintf(stderr, "Seed '%s' has no transitions\n", path);
        free(buf);
        return false;
    }

    char msg[256];
    if (!validate_transition_sequence(buf, (size_t)count, dimension, msg,
                                      sizeof(msg))) {
        fprintf(stderr, "Invalid seed '%s': %s\n", path, msg);
        free(buf);
        return false;
    }
    canonicalize_transitions(buf, (size_t)count);

    *out_trans = buf;
    *out_len = (size_t)count;
    return true;
}

/* ---- Seeded beam search with stochastic pruning ------------------------ */

static bool extend_search(int dimension, const int *seed_trans, size_t seed_len,
                          double memory_limit_gb, double epsilon, bool verbose,
                          SnakeNode *out)
{
    NodeList current_level;
    nodelist_init(&current_level);

    SnakeNode node;
    if (!snake_node_init(&node, seed_trans, seed_len, dimension)) {
        return false;
    }
    if (!nodelist_push(&current_level, node)) {
        snake_node_free(&node);
        return false;
    }

    bool have_best = true;
    SnakeNode best_snake;
    size_t max_length = seed_len;
    if (!snake_node_init(&best_snake, seed_trans, seed_len, dimension)) {
        nodelist_free(&current_level);
        return false;
    }

    int level_count = 0;
    clock_t start_time = clock();
    long total_nodes_explored = 0;

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

        size_t soft_cap = 0;

        for (size_t i = 0; i < current_level.count; i++) {
            SnakeNode *cur = &current_level.nodes[i];

            long n_legal = get_legal_next_dimensions(cur->transition_sequence,
                                                     cur->length, legal_dims);

            for (long k = 0; k < n_legal; k++) {
                int dim = legal_dims[k];

                if (snake_node_can_extend(cur, dim)) {
                    SnakeNode child;
                    if (!snake_node_create_child(cur, dim, &child)) {
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

            snake_node_free(&current_level.nodes[i]);

            if (soft_cap == 0 && next_level.count > 0) {
                size_t bpn = estimate_node_size(&next_level.nodes[0]);
                size_t max_nodes =
                    (size_t)((memory_limit_gb * 1024.0 * 1024.0 * 1024.0) / bpn);
                if (max_nodes == 0) max_nodes = 1;
                soft_cap = max_nodes + max_nodes / 4;
            }
            if (soft_cap != 0 && next_level.count > soft_cap) {
                prune_stochastic(&next_level, memory_limit_gb, epsilon);
            }
        }

        if (estimate_memory_usage(&next_level) > memory_limit_gb) {
            if (verbose) {
                printf("Level %d: Pruning %zu nodes to fit memory limit\n",
                       level_count + 1, next_level.count);
            }
            prune_stochastic(&next_level, memory_limit_gb, epsilon);
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
        *out = best_snake;
        return true;
    }
    return false;
}

/* ---- Driver ------------------------------------------------------------ */

static bool is_number(const char *s)
{
    char *end;
    strtod(s, &end);
    return *s != '\0' && *end == '\0';
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <target_dimension> [memory_limit_gb] [--epsilon E]\n"
        "                              [--seed N] [--trials T] [seed_file]\n"
        "  memory_limit_gb : approximate peak RSS cap for the beam (default 18.0).\n"
        "  --epsilon E     : fraction (0..1) of survivors chosen at random each\n"
        "                    level instead of by fitness (default 0.1). E=0\n"
        "                    reproduces pruned_bfs_search's exact behaviour;\n"
        "                    E=1 is pure random pruning.\n"
        "  --seed N        : RNG seed (default: time-based).\n"
        "  --trials T      : run T independent trials, keep the best (default 1).\n"
        "  seed_file       : text file of transition integers (default\n"
        "                    extend_input.txt).\n",
        prog);
}

/* Print at most a small prefix of the Hamming grid, or skip it for large
 * snakes -- unlike extend_snake.c/check_snake.c, which print it unconditionally
 * and can blow up to multi-GB output for large n (see CLAUDE.md, 2026-07-25/26
 * incident log). This is new code, not an edit to the original tool. */
#define GRID_PRINT_MAX_LEN 500

static void print_grid_guarded(const long *vertices, size_t n)
{
    if (n > GRID_PRINT_MAX_LEN) {
        printf("(Hamming grid skipped: %zu vertices exceeds the %d-vertex "
               "print cap to avoid an O(n^2) output blowup.)\n",
               n, GRID_PRINT_MAX_LEN);
        return;
    }
    print_hamming_grid(vertices, n);
}

int main(int argc, char **argv)
{
    snake_io_set_base(argv[0]);
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
    double epsilon = 0.1;
    unsigned int rng_seed = (unsigned int)time(NULL);
    bool seed_set = false;
    int trials = 1;
    const char *seed_path = "extend_input.txt";
    bool path_set = false;

    for (int ai = 2; ai < argc; ai++) {
        const char *a = argv[ai];
        if (strcmp(a, "--epsilon") == 0 && ai + 1 < argc) {
            epsilon = atof(argv[++ai]);
            continue;
        }
        if (strcmp(a, "--seed") == 0 && ai + 1 < argc) {
            rng_seed = (unsigned int)strtoul(argv[++ai], NULL, 10);
            seed_set = true;
            continue;
        }
        if (strcmp(a, "--trials") == 0 && ai + 1 < argc) {
            trials = atoi(argv[++ai]);
            continue;
        }
        if (strcmp(a, "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (!mem_set && !path_set && is_number(a)) {
            memory_limit_gb = atof(a);
            mem_set = true;
            continue;
        }
        seed_path = a;
        path_set = true;
    }

    if (epsilon < 0.0) epsilon = 0.0;
    if (epsilon > 1.0) epsilon = 1.0;
    if (trials < 1) trials = 1;

    int *seed_trans;
    size_t seed_len;
    if (!load_seed(seed_path, dimension, &seed_trans, &seed_len)) {
        return 1;
    }

    printf("Snake-in-the-box seeded beam search (STOCHASTIC pruning)\n");
    printf("Dimension: %d, memory limit: %.1f GB, epsilon: %.3f, trials: %d\n",
           dimension, memory_limit_gb, epsilon, trials);
    printf("Seed: %s (length %zu)\n\n", seed_path, seed_len);

    SnakeNode overall_best;
    bool have_overall_best = false;
    size_t overall_best_len = seed_len;

    for (int t = 0; t < trials; t++) {
        unsigned int this_seed = seed_set ? (rng_seed + (unsigned int)t)
                                          : (unsigned int)(time(NULL) + t * 7919u);
        srand(this_seed);
        printf("--- Trial %d/%d (rng seed %u) ---\n", t + 1, trials, this_seed);

        SnakeNode trial_best;
        bool ok = extend_search(dimension, seed_trans, seed_len, memory_limit_gb,
                                epsilon, true, &trial_best);
        if (!ok) {
            fprintf(stderr, "Trial %d: search failed.\n", t + 1);
            continue;
        }

        size_t trial_len = snake_node_get_length(&trial_best);
        printf("Trial %d result: length %zu\n\n", t + 1, trial_len);

        if (!have_overall_best || trial_len > overall_best_len) {
            if (have_overall_best) snake_node_free(&overall_best);
            overall_best = trial_best;
            overall_best_len = trial_len;
            have_overall_best = true;
        } else {
            snake_node_free(&trial_best);
        }
    }

    free(seed_trans);

    if (!have_overall_best) {
        fprintf(stderr, "All trials failed.\n");
        return 1;
    }

    size_t len = snake_node_get_length(&overall_best);
    printf("=== Best across %d trial(s): length %zu ===\n", trials, len);

    printf("Transitions: ");
    for (size_t i = 0; i < len; i++) {
        printf("%d ", overall_best.transition_sequence[i]);
    }
    printf("\n");

    char msg[256];
    long *vertices = (long *)malloc((len + 1) * sizeof(long));
    if (vertices != NULL) {
        long n = transition_to_vertex(overall_best.transition_sequence, len,
                                      dimension, 0, vertices);
        bool valid = validate_snake(vertices, (size_t)n, msg, sizeof(msg));
        printf("Validation:  %s (%s)\n", valid ? "VALID" : "INVALID", msg);
        print_grid_guarded(vertices, (size_t)n);
        free(vertices);
    }

    save_snake_result(overall_best.transition_sequence, len, dimension);

    snake_node_free(&overall_best);
    return 0;
}
