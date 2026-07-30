#!/usr/bin/env python3
"""Independent snake validator — O(N * dimension), bounded output, no crash risk.

check_snake's own validation is correct but its companion Hamming-distance grid
print is O(N^2) with no size cap, which blew up memory for large snakes (see
CLAUDE.md's dim17 incident) and has to be killed mid-run to avoid it. This script
checks the same two properties a different way, independent of this repo's C
validation code (own reimplementation, not a call into transitions.c/validation.c),
so it also serves as a cross-check against a bug shared by that code path:

1. Distinctness: walk the transition sequence into vertices (XOR one bit per
   step) and check no vertex repeats, via a dict (O(N) average, not an O(N^2)
   pairwise scan).
2. Induced-path property: for every vertex, its `dimension` hypercube neighbors
   (each differing by exactly one bit) that are also in the snake must be its
   immediate predecessor/successor in the path — not some other, non-adjacent
   position. This is the direct definition of "induced path" and only costs
   O(dimension) lookups per vertex, O(N * dimension) total.

Output is always a single bounded verdict line (plus one line per violation, if
any) — never a size-dependent dump, so nothing here needs to be killed early.

Usage:
    validate_snake_fast.py <dimension> <transitions_file>
"""
import sys
from pathlib import Path


def main(argv):
    if len(argv) != 3:
        sys.exit(f"Usage: {argv[0]} <dimension> <transitions_file>")

    dimension = int(argv[1])
    transitions = [int(t) for t in Path(argv[2]).read_text().split()]

    vertex = 0
    vertices = [vertex]
    for i, t in enumerate(transitions):
        if not (0 <= t < dimension):
            print(f"INVALID: transition {i} has value {t}, "
                  f"must be in range [0, {dimension})")
            return 1
        vertex ^= (1 << t)
        vertices.append(vertex)

    n = len(vertices)

    pos = {}
    for i, v in enumerate(vertices):
        if v in pos:
            print(f"INVALID: vertex {v} repeated at positions {pos[v]} and {i}")
            return 1
        pos[v] = i

    for i, v in enumerate(vertices):
        for bit in range(dimension):
            neighbor = v ^ (1 << bit)
            j = pos.get(neighbor)
            if j is not None and abs(i - j) != 1:
                print(f"INVALID: positions {i} and {j} (vertices {v}, {neighbor}) "
                      f"are hypercube-adjacent but not consecutive in the path "
                      f"(chord found)")
                return 1

    print(f"VALID: {n} vertices, {len(transitions)} transitions, "
          f"dimension {dimension} (induced path confirmed, no repeats, no chords)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
