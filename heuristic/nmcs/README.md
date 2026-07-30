# nmcs

Experimental sibling of `../pruned_bfs_search/` and `../stochastic_prune/`.
Implements Nested Monte Carlo Search (NMCS) instead of a fitness-pruned beam.
See `../../notes/search-strategy-ideas.md` (idea #5) and `nmcs_extend.c`'s header
comment for the algorithm and reasoning; D. Kinny's ECAI 2012 paper is the
precedent this borrows its move-ordering heuristic from (prefer moves leading to
a "forced" state with exactly one further legal continuation).

Does not touch `../pruned_bfs_search/` or `../stochastic_prune/` — its own copies
of the shared utility modules, no shared code or state with either.

Key structural difference from the beam-search tools: memory use is O(depth) per
search, not O(beam width) — there is no huge candidate list to prune. This trades
RAM for repeated-rollout compute, which is attractive given this project hit real
memory-scaling limits (and swap thrashing) with the beam search this session.

## Build

```
make
```

## Usage

```
./nmcs_extend <target_dimension> [--level L] [--seed N] [--trials T] [seed_file]
```

- `--level L` — NMCS nesting level (default 1). Level 0 is a plain randomized
  playout (still uses the forced-move heuristic, but no nested search). Cost
  grows roughly as `dimension^level x path_length` per trial — dimension (the
  branching factor) is small for the dimensions this project targets, so level
  0/1 are cheap (well under a second even for paths of length ~400); level 2+
  gets expensive fast.
- `--seed N` — RNG seed (default: time-based, so repeated runs differ).
- `--trials T` — run T independent searches, keep the longest (default 1).
  Since a single trial is so cheap relative to the beam-search tools, running
  many trials here is the natural way to spend a time/compute budget.
- `seed_file` — text file of transition integers (same format as the other
  tools' `seeds/` files). Default `extend_input.txt`.

Output saves to `../seeds/` and `../snakes/`, same convention as the other tools
(filenames auto-disambiguate on collision). The final Hamming-grid print is
capped at 500 vertices (see `stochastic_prune/README.md` for why) rather than
printed unconditionally.
