/* snake_node.h - SnakeNode for search tree representation.
 *
 * C translation of snake_in_box/core/snake_node.py
 *
 * Each node maintains:
 *   - The transition sequence representing the snake path
 *   - A bitmap marking occupied and prohibited vertices
 *   - Fitness score (count of unmarked vertices)
 */
#ifndef SNAKE_NODE_H
#define SNAKE_NODE_H

#include <stdbool.h>
#include <stddef.h>

#include "hypercube.h"

/* Node in the search tree representing a snake.
 *
 * Fields
 * ------
 * transition_sequence : Heap-allocated array of bit positions (the snake path)
 * length              : Number of transitions in the sequence
 * dimension           : Dimension of the hypercube
 * vertices_bitmap     : Bitmap tracking vertex states
 * fitness             : Count of unmarked (available) vertices
 */
typedef struct {
    int *transition_sequence;
    size_t length;
    int dimension;
    HypercubeBitmap vertices_bitmap;
    long fitness;
} SnakeNode;

/* Initialize a snake node from a transition sequence (copied internally).
 * trans_len may be 0 (empty snake at origin). Returns true on success. */
bool snake_node_init(SnakeNode *node, const int *transition_sequence,
                     size_t trans_len, int dimension);

/* Release memory held by the node. */
void snake_node_free(SnakeNode *node);

/* Get current vertex (end of snake path). */
long snake_node_get_current_vertex(const SnakeNode *node);

/* Check if snake can be extended with given dimension. */
bool snake_node_can_extend(const SnakeNode *node, int new_dimension);

/* Create child node (out) by extending snake with new_dimension.
 * Returns true on success, false if the extension is invalid or allocation
 * fails. out must be uninitialized. */
bool snake_node_create_child(const SnakeNode *node, int new_dimension,
                             SnakeNode *out);

/* Get snake length (number of edges). */
size_t snake_node_get_length(const SnakeNode *node);

#endif /* SNAKE_NODE_H */
