/* priming.c - Incremental priming: extend a snake one dimension at a time.
 *
 * C translation of snake_in_box/search/priming.py (prime_search).
 *
 * Where extend_snake.c seeds the beam once and jumps straight to a target
 * dimension, this tool follows the paper's priming strategy literally: it steps
 * from the seed's own dimension up to the target dimension ONE dimension at a
 * time. At each step the best snake found so far becomes the seed for a pruned
 * BFS in the next dimension; if that search cannot extend it, priming stops and
 * returns the best snake found so far (a valid lower bound), matching
 * prime_search's fallback behaviour.
 *
 * A snake's transition sequence is still a valid snake when embedded in Q_D for
 * any D >= its own dimension, so it is a legitimate seed. The seed is read as
 * space/newline-separated transition integers from a text file (default
 * extend_input.txt) - paste a snake exactly as the beam's main.c prints it after
 * "Transitions:".
 *
 * This file is self-contained: like extend_snake.c it carries its own copy of
 * the beam machinery (NodeList + fitness pruning + level loop), kept static so
 * there is no duplicate symbol. It calls snake_node_can_extend directly rather
 * than redefining is_valid_extension.
 *
 * Usage:
 *   ./priming <target_dimension> [memory_limit_gb] [input_file]
 *   (defaults: memory_limit_gb = 18.0, input_file = extend_input.txt)
 */
#include "snake_node.h"
#include "canonical.h"
#include "transitions.h"
#include "validation.h"
#include "snake_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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

/* ---- Seeded beam search (one dimension) ------------------------------- */

/* Run the pruned BFS in dimension `dimension`, seeded with the snake given by
 * `seed_transitions` (length `seed_len`) instead of the empty root. Mirrors
 * pruned_bfs_search_from_seed in priming.py (core loop, without the Python
 * high-dimension backtracking heuristics). On success writes the longest snake
 * found to *out (caller frees) and returns true. */
static bool extend_search(int dimension, const int *seed_transitions,
                          size_t seed_len, double memory_limit_gb, bool verbose,
                          SnakeNode *out)
{
    NodeList current_level;
    nodelist_init(&current_level);

    SnakeNode seed;
    if (!snake_node_init(&seed, seed_transitions, seed_len, dimension)) {
        return false;
    }
    if (!nodelist_push(&current_level, seed)) {
        snake_node_free(&seed);
        return false;
    }

    /* The seed itself is the initial incumbent. */
    bool have_best = true;
    SnakeNode best_snake;
    size_t max_length = seed_len;
    if (!snake_node_init(&best_snake, seed_transitions, seed_len, dimension)) {
        nodelist_free(&current_level);
        return false;
    }

    int level_count = 0;

    int *legal_dims = (int *)malloc((size_t)(dimension + 1) * sizeof(int));
    if (legal_dims == NULL) {
        nodelist_free(&current_level);
        snake_node_free(&best_snake);
        return false;
    }

    while (current_level.count > 0) {
        NodeList next_level;
        nodelist_init(&next_level);

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
                    }
                }
            }
        }

        if (estimate_memory_usage(&next_level) > memory_limit_gb) {
            if (verbose) {
                printf("  Level %d: Pruning %zu nodes to fit memory limit\n",
                       level_count + 1, next_level.count);
            }
            prune_by_fitness(&next_level, memory_limit_gb);
        }

        nodelist_free(&current_level);
        current_level = next_level;
        level_count += 1;

        if (current_level.count == 0) {
            break;
        }
    }

    free(legal_dims);
    nodelist_free(&current_level);

    if (have_best) {
        *out = best_snake;  /* transfer ownership to caller */
        return true;
    }
    return false;
}

/* ---- Priming --------------------------------------------------------- */

/* Determine dimension from a transition sequence: one more than the maximum
 * transition value (transitions are 0-indexed). Empty sequence -> 1.
 * Mirrors detect_dimension in priming.py. */
static int detect_dimension(const int *transitions, size_t len)
{
    if (len == 0) {
        return 1;
    }
    int max_trans = transitions[0];
    for (size_t i = 1; i < len; i++) {
        if (transitions[i] > max_trans) {
            max_trans = transitions[i];
        }
    }
    return max_trans + 1;
}

/* Extend `seed` (length seed_len) up to `target_dimension`, one dimension at a
 * time. On return *out holds the best snake found (caller frees) and *out_len
 * its length; returns true on success. If a step cannot extend, priming stops
 * early and returns the best snake found so far (a valid lower bound). */
static bool prime_search(const int *seed, size_t seed_len, int target_dimension,
                         double memory_limit_gb, bool verbose,
                         int **out, size_t *out_len)
{
    /* current_snake holds the running best transition sequence (owned). */
    int *current_snake = (int *)malloc(seed_len * sizeof(int));
    if (current_snake == NULL) {
        return false;
    }
    for (size_t i = 0; i < seed_len; i++) {
        current_snake[i] = seed[i];
    }
    size_t current_len = seed_len;
    int current_dim = detect_dimension(current_snake, current_len);

    if (current_dim >= target_dimension) {
        if (verbose) {
            printf("Snake already in dimension %d, target is %d\n",
                   current_dim, target_dimension);
        }
        *out = current_snake;
        *out_len = current_len;
        return true;
    }

    while (current_dim < target_dimension) {
        if (verbose) {
            printf("Extending from dimension %d to %d\n",
                   current_dim, current_dim + 1);
        }

        SnakeNode extended;
        bool ok = extend_search(current_dim + 1, current_snake, current_len,
                                memory_limit_gb, verbose, &extended);
        if (!ok) {
            if (verbose) {
                printf("Search failed to extend from dimension %d, "
                       "using seed as fallback\n", current_dim);
            }
            break;
        }

        size_t new_len = snake_node_get_length(&extended);
        if (new_len > current_len) {
            /* Adopt the extended snake as the new running best. */
            int *grown = (int *)malloc(new_len * sizeof(int));
            if (grown == NULL) {
                snake_node_free(&extended);
                free(current_snake);
                return false;
            }
            for (size_t i = 0; i < new_len; i++) {
                grown[i] = extended.transition_sequence[i];
            }
            free(current_snake);
            current_snake = grown;
            current_len = new_len;
            current_dim += 1;
            snake_node_free(&extended);

            if (verbose) {
                printf("Extended to dimension %d, length: %zu\n",
                       current_dim, current_len);
            }
        } else {
            /* Search ran but found no extension: stop with current snake. */
            if (verbose) {
                printf("No extension found from dimension %d, using seed\n",
                       current_dim);
            }
            snake_node_free(&extended);
            break;
        }
    }

    *out = current_snake;
    *out_len = current_len;
    return true;
}

/* ---- Seed input ------------------------------------------------------- */

/* Read space/newline-separated transition integers from `path` into `out`
 * (capacity `cap`). Returns the count read, or -1 on error. */
static long read_seed(const char *path, int *out, long cap)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "Could not open seed file '%s'\n", path);
        return -1;
    }

    long count = 0;
    int value;
    while (fscanf(f, "%d", &value) == 1) {
        if (count >= cap) {
            fprintf(stderr, "Seed too long (more than %ld transitions)\n", cap);
            fclose(f);
            return -1;
        }
        out[count++] = value;
    }

    /* Reject trailing non-integer junk (anything left that is not whitespace). */
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            fprintf(stderr, "Seed file contains non-integer text\n");
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return count;
}

/* ---- Driver ----------------------------------------------------------- */

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr,
                "Usage: %s <target_dimension> [memory_limit_gb] [input_file]\n",
                argv[0]);
        return 1;
    }

    int target_dimension = atoi(argv[1]);
    double memory_limit_gb = (argc > 2) ? atof(argv[2]) : 18.0;
    const char *input_file = (argc > 3) ? argv[3] : "extend_input.txt";

    if (target_dimension < 1) {
        fprintf(stderr, "Target dimension must be >= 1, got %d\n",
                target_dimension);
        return 1;
    }

    /* Read the seed snake (at most 2^target_dimension transitions can fit). */
    long cap = (long)1 << target_dimension;
    int *seed = (int *)malloc((size_t)cap * sizeof(int));
    if (seed == NULL) {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }
    long seed_len = read_seed(input_file, seed, cap);
    if (seed_len < 0) {
        free(seed);
        return 1;
    }
    if (seed_len == 0) {
        fprintf(stderr, "Seed file '%s' contained no transitions\n", input_file);
        free(seed);
        return 1;
    }

    /* Validate the seed is a real snake in the target dimension. This also
     * catches transitions out of range for the target dimension. */
    char msg[256];
    if (!validate_transition_sequence(seed, (size_t)seed_len, target_dimension,
                                      msg, sizeof(msg))) {
        fprintf(stderr, "Seed is not a valid snake in dimension %d: %s\n",
                target_dimension, msg);
        free(seed);
        return 1;
    }

    int seed_dim = detect_dimension(seed, (size_t)seed_len);

    printf("Snake-in-the-box priming search (C translation)\n");
    printf("Target dimension: %d, memory limit: %.1f GB\n",
           target_dimension, memory_limit_gb);
    printf("Priming seed snake of length %ld (dimension %d) up to dimension %d\n\n",
           seed_len, seed_dim, target_dimension);

    int *result = NULL;
    size_t result_len = 0;
    bool ok = prime_search(seed, (size_t)seed_len, target_dimension,
                           memory_limit_gb, true, &result, &result_len);
    free(seed);
    if (!ok) {
        fprintf(stderr, "Priming failed.\n");
        free(result);
        return 1;
    }

    printf("\nFinal snake of length %zu\n", result_len);

    printf("Transitions: ");
    for (size_t i = 0; i < result_len; i++) {
        printf("%d ", result[i]);
    }
    printf("\n");

    long *vertices = (long *)malloc((result_len + 1) * sizeof(long));
    if (vertices != NULL) {
        long n = transition_to_vertex(result, result_len, target_dimension,
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

    save_snake_result(result, result_len, target_dimension);

    free(result);
    return 0;
}
