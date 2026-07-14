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

#endif /* VALIDATION_H */
