/* bfs_pruned.h - Heuristically-pruned breadth-first search algorithm.
 *
 * C translation of snake_in_box/search/bfs_pruned.py
 *
 * This implements the algorithm from Ace (2025) that discovered record-breaking
 * snakes in dimensions 11-13. The search performs level-by-level expansion of a
 * search tree, pruning nodes when memory constraints are exceeded based on a
 * fitness heuristic (count of unmarked vertices).
 */
#ifndef BFS_PRUNED_H
#define BFS_PRUNED_H

#include <stdbool.h>

#include "snake_node.h"

/* Execute heuristically-pruned breadth-first search for snake-in-the-box.
 *
 * dimension       : Dimension of hypercube to search (n in Q_n)
 * memory_limit_gb : Maximum memory usage in gigabytes (default 18.0)
 * verbose         : Print progress information
 *
 * On success the best snake found is written to *out (caller must
 * snake_node_free it) and the function returns true. Returns false if no snake
 * was found or on allocation failure.
 */
bool pruned_bfs_search(int dimension, double memory_limit_gb, bool verbose,
                       SnakeNode *out);

/* Check if snake can be extended with given dimension. */
bool is_valid_extension(const SnakeNode *node, int new_dimension);

#endif /* BFS_PRUNED_H */
