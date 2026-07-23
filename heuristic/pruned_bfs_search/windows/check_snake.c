/* check_snake.c - Validate one transition sequence and draw its Hamming grid.
 *
 * A tiny standalone checker for an individual snake: give it a dimension and a
 * transition sequence, and it reports whether the sequence is a valid snake and
 * prints the pairwise Hamming-distance grid (small snakes) or a verdict-only
 * summary (large ones). In a valid snake the only 1s in the grid lie on the two
 * diagonals adjacent to the main diagonal.
 *
 * Usage:
 *   ./check_snake <dimension> <t0> <t1> <t2> ...   transitions as arguments
 *   ./check_snake <dimension>                      transitions read from stdin
 *
 * The stdin form reads whitespace-separated integers, so it accepts the repo's
 * bare-integer seed files directly:
 *   ./check_snake 6 < seeds/6D_L26.txt
 *   cat seeds/6D_L26.txt | ./check_snake 6
 */
#include <stdio.h>
#include <stdlib.h>

#include "validation.h"

/* Read whitespace-separated integers from `f` into a growable array. Returns the
 * array (caller frees) and writes the count to *out_len; NULL on allocation
 * failure. A count of 0 with a non-NULL return is a valid empty sequence. */
static int *read_ints(FILE *f, size_t *out_len)
{
    size_t cap = 16, len = 0;
    int *buf = (int *)malloc(cap * sizeof(int));
    if (buf == NULL) return NULL;

    int value;
    while (fscanf(f, "%d", &value) == 1) {
        if (len == cap) {
            size_t new_cap = cap * 2;
            int *grown = (int *)realloc(buf, new_cap * sizeof(int));
            if (grown == NULL) { free(buf); return NULL; }
            buf = grown;
            cap = new_cap;
        }
        buf[len++] = value;
    }

    *out_len = len;
    return buf;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr,
            "Usage: %s <dimension> [t0 t1 t2 ...]\n"
            "  With transitions as arguments, or omitted to read them (as\n"
            "  whitespace-separated integers) from stdin, e.g.\n"
            "    %s 6 0 1 2 0 1 3\n"
            "    %s 6 < seeds/6D_L26.txt\n",
            argv[0], argv[0], argv[0]);
        return 2;
    }

    int dimension = atoi(argv[1]);
    if (dimension < 1) {
        fprintf(stderr, "Dimension must be >= 1, got %d\n", dimension);
        return 2;
    }

    /* Transitions either follow on the command line or arrive on stdin. */
    int *trans = NULL;
    size_t len = 0;
    if (argc > 2) {
        len = (size_t)(argc - 2);
        trans = (int *)malloc((len ? len : 1) * sizeof(int));
        if (trans == NULL) { fprintf(stderr, "Out of memory\n"); return 1; }
        for (size_t i = 0; i < len; i++) trans[i] = atoi(argv[i + 2]);
    } else {
        trans = read_ints(stdin, &len);
        if (trans == NULL) { fprintf(stderr, "Out of memory\n"); return 1; }
    }

    printf("Dimension: %d, transitions: %zu\n", dimension, len);
    printf("Transitions: ");
    for (size_t i = 0; i < len; i++) printf("%d ", trans[i]);
    printf("\n");

    char msg[256];
    bool valid = validate_transition_sequence(trans, len, dimension,
                                              msg, sizeof(msg));
    printf("Validation:  %s (%s)\n", valid ? "VALID" : "INVALID", msg);

    print_hamming_grid_transitions(trans, len, dimension);

    free(trans);
    return valid ? 0 : 1;
}
