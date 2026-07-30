# nmcs_fitness

Experimental sibling of `../pruned_bfs_search/`, `../stochastic_prune/`, and
`../nmcs/`. Replaces vanilla NMCS's forced-move playout heuristic with a
fitness-weighted (softmax) move selection. See `nmcs_fitness_extend.c`'s
header comment for the full design reasoning and `CLAUDE.md`'s 2026-07-26
"nmcs_fitness" log for the result.

**Status: negative result, root cause understood.** The "count of unmarked
vertices" fitness signal is exactly tied across every legal candidate at a
single step (a structural property of the hypercube's local symmetry, verified
with a debug build, not a bug) — so this signal has zero one-step
discriminating power, and softmax over always-tied values reduces to uniform
random regardless of temperature. See `../nmcs_lookahead/` for the follow-up
that tried fixing this with multi-step probing instead (also negative, for a
different reason).

Does not touch any other tool — its own copies of the shared utility modules.

## Build

```
make
```

## Usage

```
./nmcs_fitness_extend <target_dimension> [--level L] [--temperature T]
    [--seed N] [--trials T2] [seed_file]
```

- `--level L` — NMCS nesting level (default 0).
- `--temperature T` — softmax temperature over fitness (default 5.0). Lower =
  greedier, higher = closer to uniform. Given the tied-fitness finding above,
  this parameter turned out not to matter in practice at level 0.
- `--seed N` — RNG seed (default: time-based).
- `--trials T2` — run T2 independent searches, keep the best (default 1).
- `seed_file` — text file of transition integers (default `extend_input.txt`).

Output saves to `../seeds/` and `../snakes/`, same convention as the other
tools. Hamming-grid print capped at 500 vertices.
