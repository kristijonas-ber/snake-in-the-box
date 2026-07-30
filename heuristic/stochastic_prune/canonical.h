/* canonical.h - Canonical form utilities for symmetry reduction.
 *
 * C translation of snake_in_box/utils/canonical.py
 *
 * Canonical form (Kochut) ensures exactly one representative from each
 * equivalence class of snakes related by hypercube symmetries:
 *   - First digit must be 0
 *   - Each subsequent digit must be <= max_dimension_used + 1
 */
#ifndef CANONICAL_H
#define CANONICAL_H

#include <stdbool.h>
#include <stddef.h>

/* Check if transition sequence follows Kochut's canonical form. */
bool is_canonical(const int *transition_sequence, size_t trans_len);

/* Get legal next dimensions for canonical extension.
 *
 * For canonical form, the next dimension can be any previously used dimension
 * or exactly one more than the maximum dimension used so far. Results are
 * written to out (sorted ascending); out must have room for (max_dim + 2)
 * entries, which never exceeds (trans_len + 1). Returns the number written.
 */
long get_legal_next_dimensions(const int *transition_sequence, size_t trans_len,
                               int *out);

#endif /* CANONICAL_H */
