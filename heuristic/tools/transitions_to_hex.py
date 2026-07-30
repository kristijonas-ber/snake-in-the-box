#!/usr/bin/env python3
"""Convert a snake transition sequence to the minortriad.com hex-digit notation.

The analyzer at https://minortriad.com/snake/ expects a snake as its transition
sequence written as one *digit per transition*, concatenated with no separators.
Per the site's FAQ, "Dimension numbers here are zero-based hexadecimal digits":
each transition (a zero-based bit position) becomes a single base-16 digit, so
bit positions 0..15 map to 0-9 then a-f.

Dimensions 14, 15 and 16 use bit positions up to 13, 14 and 15 respectively, so
they fit hexadecimal exactly. Dimension 17 uses bit position 16, which is beyond
a hex digit; this script extends past 'f' with 'g', 'h', ... (base-36 style) as
the only sensible continuation, but note the site's analyzer is "hexadecimal" and
may not accept digits above 'f'.

Usage:
    transitions_to_hex.py INPUT.txt [OUTPUT.txt]

INPUT is a whitespace-separated list of integer transitions (this repo's seed
format). OUTPUT defaults to INPUT with a ".hex" extension. A one-line header
comment is written, followed by the digit string on its own line.
"""
import sys
from pathlib import Path

# 0-9 then a-z, so any bit position 0..35 becomes exactly one character.
DIGITS = "0123456789abcdefghijklmnopqrstuvwxyz"


def to_hex_digits(transitions):
    """Map each integer transition to a single lowercase base-36 digit."""
    out = []
    for t in transitions:
        if t < 0 or t >= len(DIGITS):
            raise ValueError(f"transition {t} has no single-digit representation")
        out.append(DIGITS[t])
    return "".join(out)


def main(argv):
    if len(argv) < 2 or len(argv) > 3:
        sys.exit(__doc__)

    in_path = Path(argv[1])
    out_path = Path(argv[2]) if len(argv) == 3 else in_path.with_suffix(".hex")

    # Read every integer in the file, ignoring any surrounding whitespace/newlines.
    transitions = [int(tok) for tok in in_path.read_text().split()]
    digits = to_hex_digits(transitions)

    hi = max(transitions)  # highest bit position used = one less than the dimension
    header = (
        f"# minortriad.com hex transition notation for {in_path.name}\n"
        f"# {len(transitions)} transitions, highest bit position {hi} "
        f"(dimension {hi + 1}); paste the line below into the analyzer\n"
    )
    out_path.write_text(header + digits + "\n")
    print(f"{in_path.name}: {len(transitions)} transitions -> {out_path}")


if __name__ == "__main__":
    main(sys.argv)
