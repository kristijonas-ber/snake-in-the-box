/* validation.c - Validation functions for snake-in-the-box solutions.
 *
 * C translation of snake_in_box/core/validation.py
 */
#include "validation.h"
#include "transitions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int hamming_distance(long a, long b)
{
    return __builtin_popcountl((unsigned long)(a ^ b));
}

static void set_msg(char *msg, size_t msg_len, const char *text)
{
    if (msg != NULL && msg_len > 0) {
        strncpy(msg, text, msg_len - 1);
        msg[msg_len - 1] = '\0';
    }
}

bool validate_snake(const long *vertex_sequence, size_t n,
                    char *msg, size_t msg_len)
{
    if (n < 2) {
        set_msg(msg, msg_len, "Valid snake (trivial case)");
        return true;
    }

    /* Check consecutive vertices have Hamming distance 1 */
    for (size_t i = 0; i < n - 1; i++) {
        int hd = hamming_distance(vertex_sequence[i], vertex_sequence[i + 1]);
        if (hd != 1) {
            if (msg != NULL) {
                snprintf(msg, msg_len,
                         "Consecutive vertices %zu and %zu have Hamming "
                         "distance %d, expected 1", i, i + 1, hd);
            }
            return false;
        }
    }

    /* Check non-consecutive vertices have Hamming distance > 1.
     * Matches the C code from the paper:
     *   for (i = 2; i <= len; i++)
     *     for (j = 0; j <= i - 2; j++) */
    for (size_t i = 2; i < n; i++) {
        for (size_t j = 0; j < i - 1; j++) {  /* j from 0 to i-2 inclusive */
            int hd = hamming_distance(vertex_sequence[i], vertex_sequence[j]);
            if (hd <= 1) {
                if (msg != NULL) {
                    snprintf(msg, msg_len,
                             "Non-consecutive vertices %zu and %zu have Hamming "
                             "distance %d, must be > 1", j, i, hd);
                }
                return false;
            }
        }
    }

    set_msg(msg, msg_len, "Valid snake");
    return true;
}

bool validate_transition_sequence(const int *transition_sequence,
                                  size_t trans_len, int dimension,
                                  char *msg, size_t msg_len)
{
    if (trans_len == 0) {
        set_msg(msg, msg_len, "Valid snake (empty sequence)");
        return true;
    }

    /* Check transitions are in valid range */
    for (size_t i = 0; i < trans_len; i++) {
        int trans = transition_sequence[i];
        if (trans < 0 || trans >= dimension) {
            if (msg != NULL) {
                snprintf(msg, msg_len,
                         "Transition %zu has value %d, must be in range [0, %d)",
                         i, trans, dimension);
            }
            return false;
        }
    }

    /* Convert to vertex sequence and validate */
    long *vertices = (long *)malloc((trans_len + 1) * sizeof(long));
    if (vertices == NULL) {
        set_msg(msg, msg_len, "Out of memory");
        return false;
    }

    long n = transition_to_vertex(transition_sequence, trans_len, dimension,
                                  0, vertices);
    if (n < 0) {
        set_msg(msg, msg_len, "Invalid transition sequence");
        free(vertices);
        return false;
    }

    bool result = validate_snake(vertices, (size_t)n, msg, msg_len);
    free(vertices);
    return result;
}

/* A full grid is O(n^2) cells; drawing thousands of vertices would flood the
 * terminal, so larger snakes get a verdict-only summary. Bump this if you really
 * want the whole grid of a big snake. */
#define HAMMING_GRID_MAX_N 64

void print_hamming_grid(const long *vertex_sequence, size_t n)
{
    if (n == 0) {
        printf("Hamming grid: (empty snake, no vertices)\n");
        return;
    }

    /* One pass for the widest distance (for column width) and the band-rule
     * violation count. The matrix is symmetric, so each unordered pair is seen
     * twice; halve at the end. A violation is an adjacent pair (|i-j|==1) whose
     * distance is not 1, or any non-adjacent pair (a chord) with distance <= 1. */
    int max_dist = 0;
    size_t violations = 0;
    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < n; j++) {
            int hd = hamming_distance(vertex_sequence[i], vertex_sequence[j]);
            if (hd > max_dist) max_dist = hd;
            if (i == j) continue;
            bool adjacent = (i + 1 == j) || (j + 1 == i);
            if (adjacent ? (hd != 1) : (hd <= 1)) violations++;
        }
    }
    violations /= 2;

    if (n > HAMMING_GRID_MAX_N) {
        printf("Hamming grid: %zu vertices exceeds the draw limit (%d); "
               "verdict only.\n", n, HAMMING_GRID_MAX_N);
        if (violations == 0)
            printf("  Band rule OK: every adjacent pair is 1, every other pair "
                   ">= 2.\n");
        else
            printf("  Band rule BROKEN: %zu violation(s).\n", violations);
        return;
    }

    /* Field width wide enough for the largest distance and the largest index. */
    int w = 1;
    for (int m = max_dist; m >= 10; m /= 10) w++;
    for (size_t m = n - 1; m >= 10; m /= 10) w++;

    printf("\nHamming-distance grid (%zu vertices). Main diagonal = 0 (shown as "
           "'.').\n", n);
    printf("Valid iff the only 1s sit on the two diagonals next to it and every "
           "other cell is >= 2.\n\n");

    /* Column-index header (the corner spans the row-label column). */
    printf("%*s ", w + 1, "");
    for (size_t j = 0; j < n; j++) printf(" %*zu", w, j);
    printf("\n");

    for (size_t i = 0; i < n; i++) {
        printf("%*zu ", w + 1, i);   /* row-index label */
        for (size_t j = 0; j < n; j++) {
            if (i == j) {
                printf(" %*s", w, ".");
            } else {
                int hd = hamming_distance(vertex_sequence[i], vertex_sequence[j]);
                printf(" %*d", w, hd);
            }
        }
        printf("\n");
    }

    if (violations == 0)
        printf("\nBand rule OK: only the diagonals are 1.\n");
    else
        printf("\nBand rule BROKEN: %zu violation(s) - a 1 lies off the "
               "diagonal, or an adjacent pair is not 1.\n", violations);
}

bool print_hamming_grid_transitions(const int *transition_sequence,
                                    size_t trans_len, int dimension)
{
    long *vertices = (long *)malloc((trans_len + 1) * sizeof(long));
    if (vertices == NULL) {
        printf("Hamming grid: out of memory\n");
        return false;
    }

    long n = transition_to_vertex(transition_sequence, trans_len, dimension,
                                  0, vertices);
    if (n < 0) {
        printf("Hamming grid: invalid transition sequence\n");
        free(vertices);
        return false;
    }

    print_hamming_grid(vertices, (size_t)n);
    free(vertices);
    return true;
}
