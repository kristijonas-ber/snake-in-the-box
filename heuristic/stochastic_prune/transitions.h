/* transitions.h - Transition and vertex sequence conversion utilities.
 *
 * C translation of snake_in_box/core/transitions.py
 *
 * A transition sequence represents which bit position changes between
 * consecutive vertices. For vertices v[i] and v[i+1], the transition is
 * log2(v[i] XOR v[i+1]).
 *
 * Sequences are passed as plain int arrays with an explicit length, replacing
 * Python's dynamic lists. Functions that build a result fill a caller-provided
 * buffer and return the number of elements written (or -1 on error).
 */
#ifndef TRANSITIONS_H
#define TRANSITIONS_H

#include <stddef.h>

/* Convert vertex sequence to transition sequence.
 *
 * out must have room for (vertex_len - 1) entries. Returns the number of
 * transitions written, or -1 if two consecutive vertices are identical or
 * differ in more than one bit.
 */
long vertex_to_transition(const long *vertex_sequence, size_t vertex_len,
                          int *out);

/* Convert transition sequence to vertex sequence.
 *
 * Starting from start_vertex, apply each transition by flipping the
 * corresponding bit position. out must have room for (trans_len + 1) entries.
 * Returns the number of vertices written, or -1 if a transition is out of
 * range [0, dimension).
 */
long transition_to_vertex(const int *transition_sequence, size_t trans_len,
                          int dimension, long start_vertex, long *out);

/* Compute current vertex from transition sequence.
 *
 * Starting from origin (0), apply all transitions to get the final vertex.
 */
long compute_current_vertex(const int *transition_sequence, size_t trans_len);

/* Parse transition sequence from hex string format.
 *
 * The paper uses hex digits (0-9, a-f) where a=10, b=11, etc. Commas and
 * whitespace are ignored. out must have room for strlen(hex_string) entries.
 * Returns the number of transitions parsed.
 */
long parse_hex_transition_string(const char *hex_string, int *out);

#endif /* TRANSITIONS_H */
