/* snake_io.h - Persist a found snake to disk.
 *
 * Saves the result of a search into two sibling folders (created if missing),
 * relative to the current working directory:
 *   seeds/<D>D_L<len>.txt   - bare transition integers, reloadable by extend_snake
 *   snakes/<D>D_L<len>.txt   - human-readable record (header + vertex sequence)
 *
 * If a file with the base title already exists, a shared numeric suffix
 * (_2, _3, ...) is used so the seed/snake pair stays aligned and nothing is
 * overwritten.
 */
#ifndef SNAKE_IO_H
#define SNAKE_IO_H

#include <stddef.h>
#include <stdbool.h>

/* Write the snake given by its transition sequence (length `len` edges) in
 * dimension `dimension` to seeds/ and snakes/. Prints the paths written.
 * Returns true on success, false on a bad argument or I/O error. */
bool save_snake_result(const int *transitions, size_t len, int dimension);

#endif /* SNAKE_IO_H */
