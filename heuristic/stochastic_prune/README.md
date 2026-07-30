# stochastic_prune

Experimental sibling of `../pruned_bfs_search/`. Same fitness-pruned beam search,
but survivor selection at each level is a tunable mix of greedy (top fitness) and
random, instead of the original's pure deterministic top-K. See
`../../notes/search-strategy-ideas.md` for the reasoning and
`stochastic_extend.c`'s header comment for the algorithm detail.

Does not touch `../pruned_bfs_search/` — builds from its own copies of the shared
utility modules (hypercube/transitions/canonical/validation/snake_node/fitness/
snake_io.{c,h}), so it shares no code or state with the original tool. If those
utilities change upstream, re-sync manually; there is no shared build dependency.

## Build

```
make
```

## Usage

```
./stochastic_extend <target_dimension> [memory_limit_gb] [--epsilon E]
                     [--seed N] [--trials T] [seed_file]
```

- `memory_limit_gb` — approximate peak RSS cap for the beam (default 18.0).
- `--epsilon E` — fraction (0..1) of each level's survivors chosen at random
  instead of by fitness (default 0.1). `E=0` reproduces the deterministic
  tool's exact behaviour; `E=1` is pure random pruning.
- `--seed N` — RNG seed (default: time-based, so repeated runs differ).
- `--trials T` — run T independent trials, keep the longest result (default 1).
  Cheaper than one T-times-wider run since each trial uses the same memory
  budget, not T times it.
- `seed_file` — text file of transition integers (same format as
  `pruned_bfs_search`'s `seeds/` files). Default `extend_input.txt`.

Output saves to `../seeds/` and `../snakes/` exactly like the original tool
(shared directories — filenames auto-disambiguate on collision).

Unlike `extend_snake`/`check_snake`, the final Hamming-grid print is capped
(`GRID_PRINT_MAX_LEN` in `stochastic_extend.c`, currently 500 vertices) and
skipped with a message above that size, to avoid the O(n²) multi-GB output
blowup documented in `CLAUDE.md`'s 2026-07-25/26 run log.
