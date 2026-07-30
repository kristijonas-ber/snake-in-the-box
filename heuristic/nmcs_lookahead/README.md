# nmcs_lookahead

Experimental sibling of `../pruned_bfs_search/`, `../stochastic_prune/`,
`../nmcs/`, and `../nmcs_fitness/`. Follow-up to `nmcs_fitness/`'s finding that
1-step fitness is useless (always tied): probes each immediate candidate
several steps ahead (cheap uniform-random continuation) before comparing
fitness, since branches should have diverged enough by then to differ. See
`nmcs_lookahead_extend.c`'s header comment for full design reasoning and
`CLAUDE.md`'s 2026-07-26 log for the result.

**Status: negative result.** The multi-step probe mechanism works exactly as
designed (verified with a debug build: real fitness differentiation appears
after just one extra probe step) — but actual search performance gets *worse*
as probe depth increases, because a single-sample random rollout is a noisier
value estimator the deeper it goes, not a better one. See `CLAUDE.md` for the
full data and the averaged-multi-probe idea that was considered but not built
(deprioritized, not worth the added cost per Kris's call).

Does not touch any other tool — its own copies of the shared utility modules.

## Compute safety

This tool has a hard **per-trial wall-clock deadline** (`--max-seconds`,
default 30s), checked inside every loop that could run long (probe loop,
playout loop, nested-search loop). A trial that exceeds it returns its
best-so-far result immediately instead of continuing or hanging — enforced by
the tool itself, not an external convention. Built in response to this
project's earlier experience with runs silently ballooning for hours.

## Build

```
make
```

## Usage

```
./nmcs_lookahead_extend <target_dimension> [--level L] [--lookahead K]
    [--temperature T] [--max-seconds S] [--seed N] [--trials T2] [seed_file]
```

- `--level L` — NMCS nesting level (default 0).
- `--lookahead K` — probe depth in steps (default 3, clamped to [1, 50]).
- `--temperature T` — softmax temperature over probed fitness (default 5.0).
- `--max-seconds S` — per-trial wall-clock budget (default 30.0).
- `--seed N` — RNG seed (default: time-based).
- `--trials T2` — run T2 independent searches, keep the best (default 1).
- `seed_file` — text file of transition integers (default `extend_input.txt`).

Output saves to `../seeds/` and `../snakes/`, same convention as the other
tools. Hamming-grid print capped at 500 vertices.
