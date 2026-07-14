/* hypercube.h - Hypercube bitmap representation for vertex tracking.
 *
 * C translation of snake_in_box/core/hypercube.py
 *
 * Memory-efficient bitmap for tracking hypercube vertices. Uses an array of
 * 64-bit unsigned integers to represent vertex states. Each bit represents one
 * vertex in the hypercube, where:
 *   - 0 = unmarked (available)
 *   - 1 = marked  (occupied or prohibited)
 */
#ifndef HYPERCUBE_H
#define HYPERCUBE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Memory-efficient bitmap for tracking hypercube vertices.
 *
 * Fields
 * ------
 * dimension    : Dimension of the hypercube (n in Q_n)
 * num_vertices : Total number of vertices (2^n)
 * num_words    : Number of 64-bit words needed to represent all vertices
 * bitmap       : Array of 64-bit unsigned integers
 */
typedef struct {
    int dimension;
    uint64_t num_vertices;
    size_t num_words;
    uint64_t *bitmap;
} HypercubeBitmap;

/* Initialize bitmap for n-dimensional hypercube.
 * Returns true on success, false on allocation failure or invalid dimension. */
bool hypercube_init(HypercubeBitmap *bm, int dimension);

/* Release memory held by the bitmap. */
void hypercube_free(HypercubeBitmap *bm);

/* Mark vertex as occupied/prohibited. */
void hypercube_set_bit(HypercubeBitmap *bm, uint64_t vertex);

/* Check if vertex is marked. */
bool hypercube_get_bit(const HypercubeBitmap *bm, uint64_t vertex);

/* Unmark vertex. */
void hypercube_clear_bit(HypercubeBitmap *bm, uint64_t vertex);

/* Count unmarked vertices (fitness function), straightforward per-vertex loop. */
uint64_t hypercube_count_unmarked(const HypercubeBitmap *bm);

/* Count unmarked vertices using popcount. */
uint64_t hypercube_count_unmarked_fast(const HypercubeBitmap *bm);

/* Clear all bits. */
void hypercube_clear_all(HypercubeBitmap *bm);

/* Create a copy of src into dst (dst must be uninitialized).
 * Returns true on success, false on allocation failure. */
bool hypercube_copy(HypercubeBitmap *dst, const HypercubeBitmap *src);

#endif /* HYPERCUBE_H */
