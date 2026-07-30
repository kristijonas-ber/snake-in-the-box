/* hypercube.c - Hypercube bitmap representation for vertex tracking.
 *
 * C translation of snake_in_box/core/hypercube.py
 *
 * The Python version raised IndexError on out-of-range vertices. In C we keep
 * the same bounds checks but report them to stderr and clamp/no-op, so the data
 * structure stays memory-safe without exceptions.
 */
#include "hypercube.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool hypercube_init(HypercubeBitmap *bm, int dimension)
{
    if (dimension < 1) {
        fprintf(stderr, "Dimension must be >= 1, got %d\n", dimension);
        return false;
    }

    bm->dimension = dimension;
    bm->num_vertices = (uint64_t)1 << dimension;          /* 2^n */
    /* Use 64-bit words, calculate number needed */
    bm->num_words = (size_t)((bm->num_vertices + 63) / 64);
    /* Initialize all bits to 0 (unmarked) */
    bm->bitmap = (uint64_t *)calloc(bm->num_words, sizeof(uint64_t));
    if (bm->bitmap == NULL) {
        fprintf(stderr, "Failed to allocate bitmap for dimension %d\n", dimension);
        return false;
    }
    return true;
}

void hypercube_free(HypercubeBitmap *bm)
{
    if (bm != NULL && bm->bitmap != NULL) {
        free(bm->bitmap);
        bm->bitmap = NULL;
    }
}

void hypercube_set_bit(HypercubeBitmap *bm, uint64_t vertex)
{
    if (vertex >= bm->num_vertices) {
        fprintf(stderr, "Vertex %llu out of range [0, %llu)\n",
                (unsigned long long)vertex, (unsigned long long)bm->num_vertices);
        return;
    }

    size_t word_idx = (size_t)(vertex >> 6);  /* Divide by 64 */
    uint64_t bit_idx = vertex & 63;           /* Modulo 64 */
    bm->bitmap[word_idx] |= ((uint64_t)1 << bit_idx);
}

bool hypercube_get_bit(const HypercubeBitmap *bm, uint64_t vertex)
{
    if (vertex >= bm->num_vertices) {
        fprintf(stderr, "Vertex %llu out of range [0, %llu)\n",
                (unsigned long long)vertex, (unsigned long long)bm->num_vertices);
        return false;
    }

    size_t word_idx = (size_t)(vertex >> 6);
    uint64_t bit_idx = vertex & 63;
    return (bm->bitmap[word_idx] & ((uint64_t)1 << bit_idx)) != 0;
}

void hypercube_clear_bit(HypercubeBitmap *bm, uint64_t vertex)
{
    if (vertex >= bm->num_vertices) {
        fprintf(stderr, "Vertex %llu out of range [0, %llu)\n",
                (unsigned long long)vertex, (unsigned long long)bm->num_vertices);
        return;
    }

    size_t word_idx = (size_t)(vertex >> 6);
    uint64_t bit_idx = vertex & 63;
    bm->bitmap[word_idx] &= ~((uint64_t)1 << bit_idx);
}

uint64_t hypercube_count_unmarked(const HypercubeBitmap *bm)
{
    uint64_t count = 0;
    for (uint64_t vertex = 0; vertex < bm->num_vertices; vertex++) {
        if (!hypercube_get_bit(bm, vertex)) {
            count += 1;
        }
    }
    return count;
}

uint64_t hypercube_count_unmarked_fast(const HypercubeBitmap *bm)
{
    uint64_t marked_bits = 0;
    for (size_t i = 0; i < bm->num_words; i++) {
        marked_bits += (uint64_t)__builtin_popcountll(bm->bitmap[i]);
    }
    return bm->num_vertices - marked_bits;
}

void hypercube_clear_all(HypercubeBitmap *bm)
{
    for (size_t i = 0; i < bm->num_words; i++) {
        bm->bitmap[i] = 0;
    }
}

bool hypercube_copy(HypercubeBitmap *dst, const HypercubeBitmap *src)
{
    if (!hypercube_init(dst, src->dimension)) {
        return false;
    }
    memcpy(dst->bitmap, src->bitmap, src->num_words * sizeof(uint64_t));
    return true;
}
