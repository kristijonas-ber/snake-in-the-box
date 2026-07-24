/* snake_io.h - Persist a found snake to disk.
 *
 * Saves the result of a search into two sibling folders (created if missing):
 *   seeds/<D>D_L<len>.txt   - bare transition integers, reloadable by extend_snake
 *   snakes/<D>D_L<len>.txt   - human-readable record (header + vertex sequence)
 *
 * The solver binaries all live in heuristic/pruned_bfs_search/, so output is
 * anchored at the track root (heuristic/) once snake_io_set_base(argv[0]) has
 * been called - the seeds/ and snakes/ folders then always land in heuristic/,
 * no matter which directory the binary was launched from. Without that call the
 * paths fall back to being relative to the current working directory.
 *
 * If a file with the base title already exists, a shared numeric suffix
 * (_2, _3, ...) is used so the seed/snake pair stays aligned and nothing is
 * overwritten.
 */
#ifndef SNAKE_IO_H
#define SNAKE_IO_H

#include <stddef.h>
#include <stdbool.h>

/* Anchor seeds/ and snakes/ at the track root using the program's argv[0].
 * The binary lives in heuristic/pruned_bfs_search/, so output is written to its
 * parent directory (heuristic/). Call once at startup, before the first
 * save_snake_result. A NULL or slash-less argv[0] leaves the CWD-relative
 * fallback in place. */
void snake_io_set_base(const char *argv0);

/* Write the snake given by its transition sequence (length `len` edges) in
 * dimension `dimension` to seeds/ and snakes/. Prints the paths written.
 * Returns true on success, false on a bad argument or I/O error. */
bool save_snake_result(const int *transitions, size_t len, int dimension);

#endif /* SNAKE_IO_H */
