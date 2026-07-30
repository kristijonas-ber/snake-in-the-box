/* snake_node.c - SnakeNode for search tree representation.
 *
 * C translation of snake_in_box/core/snake_node.py
 *
 * Note on mark_adjacent: the induced-path (snake) rule allows a candidate
 * vertex iff it is not occupied and not adjacent to any snake vertex *except
 * the current head*. We therefore prohibit the neighbours (in all dimensions)
 * of every snake vertex that has become interior, but never the neighbours of
 * the final head vertex, so the snake can still be extended forward.
 */
#include "snake_node.h"
#include "transitions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mark all vertices adjacent to 'vertex' as prohibited (in every dimension),
 * returning how many neighbours were *newly* marked (transitioned from unmarked
 * to marked). The count lets callers update a node's unmarked-vertex fitness
 * incrementally instead of rescanning the whole bitmap. */
static int mark_adjacent_counting(long vertex, HypercubeBitmap *bitmap,
                                  int dimension)
{
    int newly_marked = 0;
    for (int dim = 0; dim < dimension; dim++) {
        long adjacent = vertex ^ ((long)1 << dim);
        if (!hypercube_get_bit(bitmap, (uint64_t)adjacent)) {
            hypercube_set_bit(bitmap, (uint64_t)adjacent);
            newly_marked += 1;
        }
    }
    return newly_marked;
}

/* Mark all vertices adjacent to 'vertex' as prohibited (in every dimension). */
static void mark_adjacent(long vertex, HypercubeBitmap *bitmap, int dimension)
{
    (void)mark_adjacent_counting(vertex, bitmap, dimension);
}

/* Initialize bitmap marking occupied and prohibited vertices, following the
 * transition sequence to build the snake path. The neighbours of each vertex
 * are prohibited only once it becomes interior (i.e. just before we step off
 * it); the final head vertex's neighbours stay available so the snake can be
 * extended. Returns true on success. */
static bool initialize_bitmap(SnakeNode *node)
{
    if (!hypercube_init(&node->vertices_bitmap, node->dimension)) {
        return false;
    }
    HypercubeBitmap *bitmap = &node->vertices_bitmap;

    /* Start at origin (vertex 0) */
    long current_vertex = 0;
    hypercube_set_bit(bitmap, (uint64_t)current_vertex);

    /* Follow transition sequence to build snake path */
    for (size_t i = 0; i < node->length; i++) {
        int transition = node->transition_sequence[i];

        /* current_vertex is now interior (no longer the head): prohibit all of
         * its neighbours. */
        mark_adjacent(current_vertex, bitmap, node->dimension);

        /* Move to next vertex by flipping bit at transition position */
        current_vertex ^= ((long)1 << transition);

        /* Mark this vertex as occupied */
        hypercube_set_bit(bitmap, (uint64_t)current_vertex);
    }

    /* The final vertex is the head: its neighbours stay available. */
    return true;
}

bool snake_node_init(SnakeNode *node, const int *transition_sequence,
                     size_t trans_len, int dimension)
{
    if (dimension < 1) {
        fprintf(stderr, "Dimension must be >= 1, got %d\n", dimension);
        return false;
    }

    /* Validate transitions are in range */
    for (size_t i = 0; i < trans_len; i++) {
        int t = transition_sequence[i];
        if (t < 0 || t >= dimension) {
            fprintf(stderr, "Transition %d out of range [0, %d)\n", t, dimension);
            return false;
        }
    }

    node->dimension = dimension;
    node->length = trans_len;
    node->transition_sequence = NULL;
    if (trans_len > 0) {
        node->transition_sequence = (int *)malloc(trans_len * sizeof(int));
        if (node->transition_sequence == NULL) {
            fprintf(stderr, "Out of memory allocating transition sequence\n");
            return false;
        }
        memcpy(node->transition_sequence, transition_sequence,
               trans_len * sizeof(int));
    }

    /* Initialize bitmap, calculate fitness */
    if (!initialize_bitmap(node)) {
        free(node->transition_sequence);
        node->transition_sequence = NULL;
        return false;
    }

    node->fitness = (long)hypercube_count_unmarked_fast(&node->vertices_bitmap);
    return true;
}

void snake_node_free(SnakeNode *node)
{
    if (node == NULL) {
        return;
    }
    if (node->transition_sequence != NULL) {
        free(node->transition_sequence);
        node->transition_sequence = NULL;
    }
    hypercube_free(&node->vertices_bitmap);
}

long snake_node_get_current_vertex(const SnakeNode *node)
{
    return compute_current_vertex(node->transition_sequence, node->length);
}

bool snake_node_can_extend(const SnakeNode *node, int new_dimension)
{
    if (new_dimension < 0 || new_dimension >= node->dimension) {
        return false;
    }

    /* Compute next vertex */
    long current_vertex = snake_node_get_current_vertex(node);
    long next_vertex = current_vertex ^ ((long)1 << new_dimension);

    /* Check if next vertex is available (not marked) */
    return !hypercube_get_bit(&node->vertices_bitmap, (uint64_t)next_vertex);
}

bool snake_node_create_child(const SnakeNode *node, int new_dimension,
                             SnakeNode *out)
{
    if (!snake_node_can_extend(node, new_dimension)) {
        fprintf(stderr,
                "Cannot extend snake with dimension %d - next vertex is "
                "already marked\n", new_dimension);
        return false;
    }

    size_t new_len = node->length + 1;
    int *new_sequence = (int *)malloc(new_len * sizeof(int));
    if (new_sequence == NULL) {
        return false;
    }
    if (node->length > 0) {
        memcpy(new_sequence, node->transition_sequence,
               node->length * sizeof(int));
    }
    new_sequence[node->length] = new_dimension;

    /* Derive the child's bitmap incrementally from the parent's instead of
     * replaying the whole path from vertex 0 (as snake_node_init/initialize_bitmap
     * would). The only difference between a parent's bitmap and its child's is
     * that the parent's head vertex has now become interior, so its neighbours
     * become prohibited. The child's new head (next_vertex = head ^ (1<<dim)) is
     * itself one of those neighbours, so it is marked as a side effect - exactly
     * matching initialize_bitmap, which also sets that same bit. We therefore
     * count next_vertex's 0->1 transition once, inside the neighbour sweep, and
     * decrement the parent's unmarked-vertex fitness by that single count. */
    if (!hypercube_copy(&out->vertices_bitmap, &node->vertices_bitmap)) {
        free(new_sequence);
        return false;
    }

    long head = snake_node_get_current_vertex(node);
    int newly_marked =
        mark_adjacent_counting(head, &out->vertices_bitmap, node->dimension);

    out->transition_sequence = new_sequence;
    out->length = new_len;
    out->dimension = node->dimension;
    out->fitness = node->fitness - (long)newly_marked;
    return true;
}

size_t snake_node_get_length(const SnakeNode *node)
{
    return node->length;
}
