/* transitions.c - Transition and vertex sequence conversion utilities.
 *
 * C translation of snake_in_box/core/transitions.py
 */
#include "transitions.h"
#include "bitops.h"

#include <stdio.h>
#include <string.h>

long vertex_to_transition(const long *vertex_sequence, size_t vertex_len,
                          int *out)
{
    if (vertex_len < 2) {
        return 0;
    }

    long count = 0;
    for (size_t i = 0; i < vertex_len - 1; i++) {
        long xor_result = vertex_sequence[i] ^ vertex_sequence[i + 1];

        if (xor_result == 0) {
            fprintf(stderr,
                    "Consecutive vertices %zu and %zu are identical: %ld\n",
                    i, i + 1, vertex_sequence[i]);
            return -1;
        }

        /* Not a power of 2 - multiple bits differ */
        if ((xor_result & (xor_result - 1)) != 0) {
            fprintf(stderr,
                    "Consecutive vertices %zu and %zu differ in multiple bits\n",
                    i, i + 1);
            return -1;
        }

        /* Find bit position: log2(xor_result). For powers of 2, the index of
         * the single set bit is the trailing-zero count. */
        int bit_position = sib_ctz64((uint64_t)xor_result);
        out[count++] = bit_position;
    }

    return count;
}

long transition_to_vertex(const int *transition_sequence, size_t trans_len,
                          int dimension, long start_vertex, long *out)
{
    long count = 0;
    long current = start_vertex;
    out[count++] = start_vertex;

    for (size_t i = 0; i < trans_len; i++) {
        int transition = transition_sequence[i];
        if (transition < 0 || transition >= dimension) {
            fprintf(stderr, "Transition %d out of range [0, %d)\n",
                    transition, dimension);
            return -1;
        }

        /* Flip bit at transition position */
        current ^= ((long)1 << transition);
        out[count++] = current;
    }

    return count;
}

long compute_current_vertex(const int *transition_sequence, size_t trans_len)
{
    long vertex = 0;
    for (size_t i = 0; i < trans_len; i++) {
        vertex ^= ((long)1 << transition_sequence[i]);
    }
    return vertex;
}

long parse_hex_transition_string(const char *hex_string, int *out)
{
    static const char *hex_digits = "0123456789abcdef";
    long count = 0;

    for (const char *p = hex_string; *p != '\0'; p++) {
        char c = *p;
        /* Lowercase the character (ASCII) */
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        const char *found = strchr(hex_digits, c);
        if (found != NULL) {
            out[count++] = (int)(found - hex_digits);
        }
        /* Ignore commas, whitespace, and other characters */
    }

    return count;
}
