/* fitness.c - Fitness evaluation functions.
 *
 * C translation of snake_in_box/search/fitness.py
 *
 * The Python AdvancedFitnessEvaluator used Python sets and lists for the flood
 * fill. Here the flood fill uses a visited bitmap and an explicit ring-buffer
 * queue sized to the number of vertices.
 */
#include "fitness.h"
#include "transitions.h"

#include <stdlib.h>
#include <string.h>

long simple_fitness_evaluate(const SnakeNode *node)
{
    return node->fitness;
}

long fitness_count_unmarked_vertices(const SnakeNode *node)
{
    return node->fitness;
}

long fitness_count_dead_ends(const SnakeNode *node)
{
    long dead_ends = 0;
    long num_vertices = (long)1 << node->dimension;

    for (long vertex = 0; vertex < num_vertices; vertex++) {
        if (hypercube_get_bit(&node->vertices_bitmap, (uint64_t)vertex)) {
            continue;
        }

        /* Count unmarked neighbours */
        int unmarked_neighbors = 0;
        for (int dim = 0; dim < node->dimension; dim++) {
            long neighbor = vertex ^ ((long)1 << dim);
            if (!hypercube_get_bit(&node->vertices_bitmap, (uint64_t)neighbor)) {
                unmarked_neighbors += 1;
            }
        }

        if (unmarked_neighbors == 1) {
            dead_ends += 1;
        }
    }

    return dead_ends;
}

/* Find all reachable unmarked vertices from start using BFS (flood fill).
 * Returns the number of reachable vertices. */
static long flood_fill_reachable(const SnakeNode *node, long start_vertex)
{
    long num_vertices = (long)1 << node->dimension;

    char *visited = (char *)calloc((size_t)num_vertices, sizeof(char));
    long *queue = (long *)malloc((size_t)num_vertices * sizeof(long));
    if (visited == NULL || queue == NULL) {
        free(visited);
        free(queue);
        return 0;
    }

    long head = 0, tail = 0;
    long count = 0;
    queue[tail++] = start_vertex;

    while (head < tail) {
        long vertex = queue[head++];

        if (visited[vertex] ||
            hypercube_get_bit(&node->vertices_bitmap, (uint64_t)vertex)) {
            continue;
        }

        visited[vertex] = 1;
        count += 1;

        /* Add unmarked neighbours to the queue */
        for (int dim = 0; dim < node->dimension; dim++) {
            long neighbor = vertex ^ ((long)1 << dim);
            if (!visited[neighbor]) {
                queue[tail++] = neighbor;
            }
        }
    }

    free(visited);
    free(queue);
    return count;
}

long fitness_count_unreachable_vertices(const SnakeNode *node)
{
    long current_vertex =
        compute_current_vertex(node->transition_sequence, node->length);
    long reachable = flood_fill_reachable(node, current_vertex);
    long total_unmarked = fitness_count_unmarked_vertices(node);
    return total_unmarked - reachable;
}

double fitness_combined(const SnakeNode *node, double w_unmarked,
                        double w_dead_ends, double w_unreachable)
{
    double fitness = 0.0;
    fitness += w_unmarked * (double)fitness_count_unmarked_vertices(node);
    fitness += w_dead_ends * (double)fitness_count_dead_ends(node);
    fitness += w_unreachable * (double)fitness_count_unreachable_vertices(node);
    return fitness;
}
