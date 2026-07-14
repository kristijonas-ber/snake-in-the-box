# pruned_bfs_search — heuristic beam search for a long snake

## Quick reference

| Run | Finds |
|---|---|
| `./snake_in_box 7 18.0` | One snake in dimension 7 via direct fitness-pruned BFS (18 GB memory limit) |
| `./parallel_search 7 18.0 10` | Same search, OpenMP-parallel across 10 workers |
| `./priming 8 18.0 extend_input.txt` | Extend a seed snake one dimension at a time up to dimension 8 |
| `./extend_snake 8 18.0 extend_input.txt` | Jump a seed snake straight to dimension 8 |

```bash
make            # builds ./snake_in_box
make run        # runs the demo for dimension 7
```

## What it does

A heuristically-pruned breadth-first search: at each level it keeps only the
candidate paths with the best *fitness* (unmarked-vertex count, dead ends,
flood-fill reachability) and expands them until none can be extended further.
Direct search alone is weak (e.g. length 7 in `Q_7`) — `priming` and
`extend_snake` instead grow an existing seed snake (from the known-snakes
database or the exhaustive track's `.bin` outputs) into a higher dimension,
which is how record-length snakes are actually reached. This is a faithful C
translation of the Python `snake_in_box` package's algorithm modules.

## Parameters

All binaries take `<dimension> <memory_gb>`; the seeded tools take a seed file
as a third argument.

| Arg | Meaning |
|---|---|
| `dimension` | hypercube dimension `N` to search or extend to |
| `memory_gb` | memory budget; search prunes once exceeded |
| `workers` (`parallel_search` only) | OpenMP thread count |
| `seed_file` (`priming`/`extend_snake` only) | seed snake — a text file of transition integers, or a `.bin` file/directory from the exhaustive track |

`extend_snake` also accepts `--both-ends` and multiple seed files at once. See
file header comments for build flags (e.g. OpenMP on macOS) and the seed file
format.

## Output

Found snakes are written to `../snakes/`; their transition sequences (usable as
seeds) are written to `../seeds/`.

## Example output

A transition sequence is space-separated dimension indices, one flip per step
— e.g. `../seeds/example_dim5_len13.txt` (dimension 5, length 13):

```
0 1 2 3 0 2 4 0 1 2 0 3 1
```
