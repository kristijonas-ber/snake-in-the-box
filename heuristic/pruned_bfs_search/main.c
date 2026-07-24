/* main.c - Demonstration / smoke-test driver for the C translation.
 *
 * Mirrors the typical usage of the Python package: run a pruned BFS search for
 * a small dimension, validate the resulting snake, and print it both as a
 * transition sequence and a vertex sequence.
 *
 * Usage:
 *   ./snake_in_box [dimension] [memory_limit_gb]
 *   (defaults: dimension = 7, memory_limit_gb = 18.0)
 */
#include <stdio.h>
#include <stdlib.h>

#include "snake_node.h"
#include "bfs_pruned.h"
#include "validation.h"
#include "transitions.h"
#include "snake_io.h"

int main(int argc, char **argv)
{
    snake_io_set_base(argv[0]);  /* anchor seeds/ and snakes/ at the track root */
    int dimension = (argc > 1) ? atoi(argv[1]) : 7;
    double memory_limit_gb = (argc > 2) ? atof(argv[2]) : 18.0;

    printf("Snake-in-the-box pruned BFS search (C translation)\n");
    printf("Dimension: %d, memory limit: %.1f GB\n\n", dimension, memory_limit_gb);

    SnakeNode best;
    if (!pruned_bfs_search(dimension, memory_limit_gb, true, &best)) {
        fprintf(stderr, "Search failed to find a snake.\n");
        return 1;
    }

    size_t len = snake_node_get_length(&best);
    printf("\nFound snake of length %zu (fitness %ld)\n", len, best.fitness);

    /* Print transition sequence */
    printf("Transitions: ");
    for (size_t i = 0; i < len; i++) {
        printf("%d ", best.transition_sequence[i]);
    }
    printf("\n");

    /* Convert to vertex sequence and validate */
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
