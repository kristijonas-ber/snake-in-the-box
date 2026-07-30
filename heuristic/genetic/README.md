# genetic

Experimental sibling of `../pruned_bfs_search/`, `../stochastic_prune/`,
`../nmcs/`, `../nmcs_fitness/`, and `../nmcs_lookahead/`. A genetic algorithm
with real crossover, faithfully reimplementing the encoding and fitness
function from the primary snake-in-the-box GA literature (Potter et al. 1994;
Diaz-Gomez & Hougen, GECCO 2006 — see `genetic_extend.c`'s header comment for
full citations and design detail). Unlike the NMCS-family tools, this one does
NOT reuse `snake_node.c`/`fitness.c` — its chromosome is a flat bit-vector
(vertex-set membership), not a `SnakeNode`.

**The key design idea**: crossover on a transition-sequence or node-ordering
encoding can trivially produce an invalid path (revisited vertices, broken
adjacency). The source papers sidestep this by encoding a snake as a plain
bit-vector of length 2^dimension (bit v = "vertex v is in the snake's vertex
set") — any bitstring is syntactically valid, so ordinary crossover can never
produce a malformed chromosome. An offspring that doesn't form a valid induced
path just scores low via the fitness function's head-to-tail walk, which stops
at the first violation — no repair operator needed.

**Status: negative result, consistent with the primary literature's own
experience.** Three configurations (default, 10x mutation rate, 10x
population) all converged immediately and then plateaued completely for
hundreds of generations. See `CLAUDE.md`'s 2026-07-26 log for full data and the
important nuance (Casella & Potter's crossover-*free* PBSHC historically beat
this exact crossover-based method on this problem).

## Compute safety

Hard **wall-clock deadline** (`--max-seconds`, default 30s) checked once per
generation — a run that exceeds it returns its best individual so far.

## Build

```
make
```

## Usage

```
./genetic_extend <target_dimension> [--population P] [--generations G]
    [--mutation-rate R] [--max-seconds S] [--seed N] [seed_file]
```

- `--population P` — population size (default 200; the source papers used
  1000-10000).
- `--generations G` — generation count (default 200).
- `--mutation-rate R` — per-bit mutation probability in the mutable
  (post-seed) region (default: 1/mutable_length — the source papers don't
  specify a rate, this is a standard-GA-convention fill, not a sourced value).
- `--max-seconds S` — wall-clock budget for the whole run (default 30.0).
- `--seed N` — RNG seed (default: time-based).
- `seed_file` — text file of transition integers for the seed at dimension
  `target_dimension - 1` (default `extend_input.txt`).

Output saves to `../seeds/` and `../snakes/`, same convention as the other
tools.
