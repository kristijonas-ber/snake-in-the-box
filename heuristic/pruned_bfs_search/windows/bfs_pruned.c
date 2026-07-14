/* bfs_pruned.c - Heuristically-pruned breadth-first search algorithm.
 *
 * C translation of snake_in_box/search/bfs_pruned.py
 *
 * The Python version stored each level as a Python list of SnakeNode objects.
 * Here a level is a NodeList: a growable array of SnakeNode values. Nodes own
 * heap memory, so dropping a node (during pruning or when freeing a level)
 * always calls snake_node_free first.
 *
 * Memory accounting mirrors the Python estimate_node_size heuristic: transition
 * bytes + bitmap bytes + a fixed per-object overhead.
 */
#include "bfs_pruned.h"
#include "canonical.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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

/* Prune nodes by fitness to fit within memory limit.
 *
 * Sorts nodes by fitness (unmarked vertex count) descending and keeps only the
 * top nodes that fit within memory constraints. Dropped nodes are freed. */
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

    /* Sort by fitness (unmarked vertex count) descending */
    qsort(list->nodes, list->count, sizeof(SnakeNode), compare_fitness_desc);

    /* Free and drop the tail beyond max_nodes */
    for (size_t i = max_nodes; i < list->count; i++) {
        snake_node_free(&list->nodes[i]);
    }
    list->count = max_nodes;
}

/* ---- Public API ------------------------------------------------------- */

bool is_valid_extension(const SnakeNode *node, int new_dimension)
{
    return snake_node_can_extend(node, new_dimension);
}

bool pruned_bfs_search(int dimension, double memory_limit_gb, bool verbose,
                       SnakeNode *out)
{
    /* Initialize with empty snake at origin */
    NodeList current_level;
    nodelist_init(&current_level);

    SnakeNode root;
    if (!snake_node_init(&root, NULL, 0, dimension)) {
        return false;
    }
    if (!nodelist_push(&current_level, root)) {
        snake_node_free(&root);
        return false;
    }

    bool have_best = false;
    SnakeNode best_snake;            /* valid only when have_best is true */
    size_t max_length = 0;

    int level_count = 0;
    clock_t start_time = clock();
    long total_nodes_explored = 0;

    /* Scratch buffer for legal next dimensions (at most dimension + 1). */
    int *legal_dims = (int *)malloc((size_t)(dimension + 1) * sizeof(int));
    if (legal_dims == NULL) {
        nodelist_free(&current_level);
        return false;
    }

    while (current_level.count > 0) {
        clock_t level_start_time = clock();
        NodeList next_level;
        nodelist_init(&next_level);

        /* Generate all children for current level */
        for (size_t i = 0; i < current_level.count; i++) {
            SnakeNode *node = &current_level.nodes[i];

            /* Get legal next dimensions (canonical form) */
            long n_legal = get_legal_next_dimensions(node->transition_sequence,
                                                     node->length, legal_dims);

            for (long k = 0; k < n_legal; k++) {
                int dim = legal_dims[k];

                /* Check if extension is valid */
                if (is_valid_extension(node, dim)) {
                    SnakeNode child;
                    if (!snake_node_create_child(node, dim, &child)) {
                        /* Extension invalid / allocation failed, skip */
                        continue;
                    }
                    if (!nodelist_push(&next_level, child)) {
                        snake_node_free(&child);
                        continue;
                    }
                    total_nodes_explored += 1;

                    /* Track best snake found */
                    size_t child_length =
                        snake_node_get_length(&next_level.nodes[next_level.count - 1]);
                    if (child_length > max_length) {
                        max_length = child_length;
                        /* Keep an independent copy as the running best */
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
        }

        /* Prune if memory limit exceeded */
        if (estimate_memory_usage(&next_level) > memory_limit_gb) {
            if (verbose) {
                printf("Level %d: Pruning %zu nodes to fit memory limit\n",
                       level_count + 1, next_level.count);
            }
            prune_by_fitness(&next_level, memory_limit_gb);
        }

        /* Free memory from previous level, advance */
        nodelist_free(&current_level);
        current_level = next_level;
        level_count += 1;

        double level_elapsed =
            (double)(clock() - level_start_time) / CLOCKS_PER_SEC;

        if (verbose) {
            printf("Level %d: %zu nodes, best length: %zu, time: %.3fs\n",
                   level_count, current_level.count, max_length, level_elapsed);
        }

        /* Stop if no more nodes to expand */
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
