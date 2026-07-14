/* parallel_search.c - Parallel heuristically-pruned breadth-first search.
 *
 * C translation of snake_in_box/search/parallel.py
 *
 * The Python version distributes each BFS level across a multiprocessing.Pool:
 * the current level is split into chunks, one per worker; every worker expands
 * its chunk into a list of children and updates a shared "best snake" under a
 * lock; the children lists are then concatenated into the next level, pruned by
 * fitness, and the loop advances. This file mirrors that exactly, using OpenMP
 * threads in place of the process Pool - each thread owns a private NodeList
 * (its worker return value) and the merge / best-snake update happen in a
 * critical section (the lock).
 *
 * This file is self-contained: like extend_snake.c it carries its own copy of
 * the beam machinery (NodeList + fitness pruning + level loop) from
 * bfs_pruned.c, so bfs_pruned.c is left untouched and there is no duplicate
 * symbol. It calls snake_node_can_extend directly rather than redefining
 * is_valid_extension.
 *
 * If the compiler has no OpenMP support the code still builds and runs
 * correctly as a single-threaded search.
 *
 * Usage:
 *   ./parallel_search [dimension] [memory_limit_gb] [num_workers]
 *   (defaults: dimension = 7, memory_limit_gb = 18.0, num_workers = 10)
 */
#include "snake_node.h"
#include "canonical.h"
#include "transitions.h"
#include "validation.h"
#include "snake_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _OPENMP
#include <omp.h>
#endif

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

/* ---- Worker: expand a chunk of the current level ---------------------- */

/* Expand nodes[start, end) into `out` (a private, per-thread child list) and
 * track the longest child seen in this chunk. Mirrors expand_nodes_worker in
 * parallel.py: each worker returns its own children and its local best. On
 * return *have_best / *best hold an independent copy of the longest child (the
 * caller owns it and must snake_node_free it). */
static void expand_chunk(const NodeList *current, size_t start, size_t end,
                         NodeList *out, bool *have_best, SnakeNode *best,
                         int *legal_dims)
{
    *have_best = false;
    size_t best_length = 0;

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
    }
}

/* ---- Parallel beam search --------------------------------------------- */

/* Run the pruned BFS in dimension `dimension`, expanding each level in parallel
 * across `num_workers` threads. Identical in result to pruned_bfs_search; only
 * the per-level node expansion is distributed. On success writes the longest
 * snake found to *out (caller frees) and returns true. */
static bool parallel_search(int dimension, double memory_limit_gb,
                            int num_workers, bool verbose, SnakeNode *out)
{
#ifndef _OPENMP
    (void)num_workers;  /* num_threads() pragma is a no-op without OpenMP */
#endif

    /* Initialize with empty snake at origin. */
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

    while (current_level.count > 0) {
        clock_t level_start_time = clock();
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
            /* Contiguous chunk for this worker (matches parallel.py chunking). */
            size_t total = current_level.count;
            size_t chunk = (total + (size_t)nthreads - 1) / (size_t)nthreads;
            size_t start = (size_t)tid * chunk;
            size_t end = start + chunk;
            if (start > total) start = total;
            if (end > total) end = total;

            /* Per-worker scratch and outputs (its "return value"). */
            int *legal_dims =
                (int *)malloc((size_t)(dimension + 1) * sizeof(int));
            NodeList local_next;
            nodelist_init(&local_next);
            bool local_have_best = false;
            SnakeNode local_best;
            long local_nodes = 0;

            if (legal_dims != NULL) {
                expand_chunk(&current_level, start, end, &local_next,
                             &local_have_best, &local_best, legal_dims);
                local_nodes = (long)local_next.count;
                free(legal_dims);
            }

            /* Merge this worker's children into the shared next level and fold
             * its local best into the global best (the "lock" section). */
            #pragma omp critical
            {
                for (size_t j = 0; j < local_next.count; j++) {
                    if (!nodelist_push(&next_level, local_next.nodes[j])) {
                        snake_node_free(&local_next.nodes[j]);
                    }
                }
                /* Nodes were moved into next_level; drop the array only. */
                local_next.count = 0;
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

            /* Anything not adopted as the global best is freed here. */
            if (local_have_best) {
                snake_node_free(&local_best);
            }
            nodelist_free(&local_next);
        }

        /* Prune if memory limit exceeded. */
        if (estimate_memory_usage(&next_level) > memory_limit_gb) {
            if (verbose) {
                printf("Level %d: Pruning %zu nodes to fit memory limit\n",
                       level_count + 1, next_level.count);
            }
            prune_by_fitness(&next_level, memory_limit_gb);
        }

        /* Free memory from previous level, advance. */
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

    nodelist_free(&current_level);

    if (have_best) {
        *out = best_snake;  /* transfer ownership to caller */
        return true;
    }
    return false;
}

/* ---- Driver ----------------------------------------------------------- */

int main(int argc, char **argv)
{
    int dimension = (argc > 1) ? atoi(argv[1]) : 7;
    double memory_limit_gb = (argc > 2) ? atof(argv[2]) : 18.0;
    int num_workers = (argc > 3) ? atoi(argv[3]) : 10;

    if (num_workers < 1) {
        num_workers = 1;
    }

    printf("Snake-in-the-box parallel pruned BFS search (C translation)\n");
    printf("Dimension: %d, memory limit: %.1f GB, workers: %d\n",
           dimension, memory_limit_gb, num_workers);
#ifndef _OPENMP
    printf("(built without OpenMP - running single-threaded)\n");
#endif
    printf("\n");

    SnakeNode best;
    if (!parallel_search(dimension, memory_limit_gb, num_workers, true, &best)) {
        fprintf(stderr, "Search failed to find a snake.\n");
        return 1;
    }

    size_t len = snake_node_get_length(&best);
    printf("\nFound snake of length %zu (fitness %ld)\n", len, best.fitness);

    printf("Transitions: ");
    for (size_t i = 0; i < len; i++) {
        printf("%d ", best.transition_sequence[i]);
    }
    printf("\n");

    long *vertices = (long *)malloc((len + 1) * sizeof(long));
    if (vertices != NULL) {
        long n = transition_to_vertex(best.transition_sequence, len, dimension,
                                      0, vertices);
        printf("Vertices:    ");
        for (long i = 0; i < n; i++) {
            printf("%ld ", vertices[i]);
        }
        printf("\n");

        char msg[256];
        bool valid = validate_snake(vertices, (size_t)n, msg, sizeof(msg));
        printf("Validation:  %s (%s)\n", valid ? "VALID" : "INVALID", msg);
        free(vertices);
    }

    save_snake_result(best.transition_sequence, len, dimension);

    snake_node_free(&best);
    return 0;
}
