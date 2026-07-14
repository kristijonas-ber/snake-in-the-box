/* canonical.c - Canonical form utilities for symmetry reduction.
 *
 * C translation of snake_in_box/utils/canonical.py
 */
#include "canonical.h"

bool is_canonical(const int *transition_sequence, size_t trans_len)
{
    if (trans_len == 0) {
        return true;
    }

    /* First digit must be 0 */
    if (transition_sequence[0] != 0) {
        return false;
    }

    /* Track maximum dimension used so far */
    int max_dimension = 0;

    for (size_t i = 0; i < trans_len; i++) {
        int dim = transition_sequence[i];

        /* Each digit must be <= max_dimension + 1 */
        if (dim > max_dimension + 1) {
            return false;
        }

        /* Update max_dimension if we've introduced a new dimension */
        if (dim == max_dimension + 1) {
            max_dimension = dim;
        }
    }

    return true;
}

long get_legal_next_dimensions(const int *transition_sequence, size_t trans_len,
                               int *out)
{
    if (trans_len == 0) {
        /* Empty sequence: first transition must be 0 */
        out[0] = 0;
        return 1;
    }

    /* Get maximum dimension used */
    int max_dim = transition_sequence[0];
    for (size_t i = 1; i < trans_len; i++) {
        if (transition_sequence[i] > max_dim) {
            max_dim = transition_sequence[i];
        }
    }

    /* Can use any previously used (unique) dimension or introduce max_dim + 1.
     * Mirror Python's set(transition_sequence) + [max_dim + 1], emitted sorted.
     * 'seen' tracks presence over [0, max_dim]; index max_dim + 1 is always
     * legal as the newly introduced dimension. */
    long count = 0;
    for (int d = 0; d <= max_dim; d++) {
        bool seen = false;
        for (size_t i = 0; i < trans_len; i++) {
            if (transition_sequence[i] == d) {
                seen = true;
                break;
            }
        }
        if (seen) {
            out[count++] = d;
        }
    }
    out[count++] = max_dim + 1;  /* Can introduce next dimension */

    return count;
}
