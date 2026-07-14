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
