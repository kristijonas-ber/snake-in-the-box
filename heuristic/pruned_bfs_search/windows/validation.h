/* validation.h - Validation functions for snake-in-the-box solutions.
 *
 * C translation of snake_in_box/core/validation.py
 *
 * A valid snake must satisfy:
 *   1. Consecutive vertices have Hamming distance exactly 1
 *   2. Non-consecutive vertices have Hamming distance > 1
 *
 * The Python functions returned a (bool, message) tuple. Here the boolean is
 * the return value and an optional caller-provided message buffer receives the
 * descriptive text.
 */
#ifndef VALIDATION_H
#define VALIDATION_H

#include <stdbool.h>
#include <stddef.h>

/* Calculate Hamming distance between two integers (number of differing bits). */
int hamming_distance(long a, long b);

/* Validate that a vertex sequence represents a valid snake.
 *
 * If msg is non-NULL it receives a description (at most msg_len bytes).
 * Returns true if valid, false otherwise.
 */
bool validate_snake(const long *vertex_sequence, size_t n,
                    char *msg, size_t msg_len);

/* Validate transition sequence and convert to check snake validity.
 *
 * First converts the transition sequence to a vertex sequence, then validates
 * the resulting snake. Returns true if valid, false otherwise.
 */
bool validate_transition_sequence(const int *transition_sequence,
                                  size_t trans_len, int dimension,
                                  char *msg, size_t msg_len);

/* Print the full matrix of pairwise Hamming distances between the snake's
 * vertices, so the snake rule can be read off by eye: consecutive vertices
 * differ by 1, everything else by >= 2. In a VALID snake the only 1s therefore
 * lie on the two diagonals immediately adjacent to the main diagonal (the main
 * diagonal itself is 0, drawn as '.'); any 1 elsewhere is a chord. A verdict and
 * violation count are printed too. The grid is O(n^2) cells, so snakes larger
 * than HAMMING_GRID_MAX_N are summarised (verdict only) instead of drawn. */
void print_hamming_grid(const long *vertex_sequence, size_t n);

/* Convenience wrapper: convert a transition sequence to its origin-anchored
 * vertex path and draw its Hamming grid. Returns false on a bad transition
 * sequence or allocation failure. */
bool print_hamming_grid_transitions(const int *transition_sequence,
                                    size_t trans_len, int dimension);

#endif /* VALIDATION_H */
