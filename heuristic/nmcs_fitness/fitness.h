/* fitness.h - Fitness evaluation functions.
 *
 * C translation of snake_in_box/search/fitness.py
 *
 * The simple fitness measure (count of unmarked vertices) is the one used in
 * Ace (2025) that achieved record-breaking results. The advanced measures
 * (dead ends, unreachable vertices via flood fill) are also provided.
 */
#ifndef FITNESS_H
#define FITNESS_H

#include "snake_node.h"

/* Simple fitness: count of unmarked vertices (the node's cached fitness). */
long simple_fitness_evaluate(const SnakeNode *node);

/* Count unmarked vertices (original simple fitness). */
long fitness_count_unmarked_vertices(const SnakeNode *node);

/* Count unmarked vertices with only one unmarked neighbour (dead ends).
 * Dead ends limit future growth as they can only be entered from one side. */
long fitness_count_dead_ends(const SnakeNode *node);

/* Count vertices unreachable from the current snake position.
 * Uses flood fill (BFS) over unmarked vertices. */
long fitness_count_unreachable_vertices(const SnakeNode *node);

/* Weighted combination of multiple fitness measures.
 * Defaults (matching Python): unmarked * 1.0 + dead_ends * -0.5. Pass the three
 * weights explicitly here. */
double fitness_combined(const SnakeNode *node, double w_unmarked,
                        double w_dead_ends, double w_unreachable);

#endif /* FITNESS_H */
