# Snake-in-the-Box

The snake-in-the-box problem wants to find the longest snake—an induced path with no chords—in an $n$-dimensional hypercube. This repository covers two major approaches to this problem.

| Track | Method | Result | Reach |
|---|---|---|---|
| [`exhaustive/`](exhaustive/) | parallelized depth-first search with automorphism rejection | enumerates every longest snake | feasible up to dimension $7$ |
| [`heuristic/`](heuristic/) | fitness-pruned breadth-first search | finds maximal snakes only | no limit |

---

## Overview

This exhaustive search is able to prove the maximum length for hypercubes up to dimension $7$, along with finding the number and transition sequences of canonical longest snakes. For dimension $8$, the length of the longest snake is proven by Ostergård and Pettersson without knowing the number of canonical longest snakes.

| Dimension | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| **Longest snake (edges)** | 1 | 2 | 4 | 7 | 13 | 26 | 50 | 98 |
| **Canonical longest snakes** | 1 | 1 | 1 | 1 | 8 | 1 | 12 | N/A |

Beyond dimension $8$ the optimal longest snake length is unknown. The heuristic track chases lower bounds, without proving optimality. The table below lists the longest heuristically found snakes.

| Dimension | 9 | 10 | 11 | 12 | 13 |
|---|---|---|---|---|---|
| **Edges** | 191 | 379 | 746 | 1476 | 2924 |
| **Author** | Orland et al. | Orland et al. | Orland et al. | Orland et al. | Orland et al. |

---

## Quick start

Two scripts at the repo root build and run either track.

```bash
./run_exhaustive.sh --dim 6 --procs 5           # dimensixon 6, 5 worker nodes
./run_heuristic.sh serial 7 2.0                 # dimension 7, 2.0 GB memory budget
```

**Heuristic**

| Command | What it does |
|---|---|
| `./run_heuristic.sh serial 7 2.0` | search $Q_7$ for a snake with 2.0 GB memory budget |
| `./run_heuristic.sh parallel 7 2.0 10` | same search, spread over 10 CPU threads |
| `./run_heuristic.sh priming 8 2.0 seed.txt` | grow `seed.txt` up to $Q_8$, one dimension per step |
| `./run_heuristic.sh extend 8 2.0 --both-ends seed.txt` | grow `seed.txt` straight into $Q_8$, from both ends |

**Exhaustive**

| Command | What it does |
|---|---|
| `./run_exhaustive.sh --dim 7 --procs 10` | find every longest snake in $Q_7$ using 10 processes |
| `./run_exhaustive.sh --dim 8 --slice-count 64 --slice-id 0` | compute just slice 0 of 64 |
| `./run_exhaustive.sh --dim 8 --prefix-length 18` | generate prefixes of length 18 before full exhaustive search |
| `./run_exhaustive.sh --decode <file>.bin` | print the snakes stored in a result file |

`--help` on either script lists every option.

---

## Exhaustive — `exhaustive/dfs_search/`

The foundations of exhaustive snake-in-the-box algorithms can be found in Ville Pettersson's doctoral dissertation, *Graph Algorithms for Constructing and Enumerating Cycles and Related Structures*.

The table below lists these and additional optimization techniques for used in this repository.

| Technique | Effect |
|---|---|
| **Canonical augmentation** | a snake may only introduce dimension $k$ once $0…k−1$ are used, so of each automorphism group only one representative is ever explored |
| **Incremental chord test** | each vertex keeps a count of its snake-neighbours, adjusted in $O(N)$ as the path grows; a count of 2 forbids the new vertex, so an extension's legality is checked without ever rescanning the path |
| **Branch and bound** | a running count of still-usable vertices prunes any branch where `length + available < target` |
| **Streaming prefix generation** | prefixes are emitted by order from a deterministic prefix generator |
| **Two passes** | the first pass finds the longest length *L*, the second re-walks and writes to disk only the snakes of that length |

Snakes are written as one byte per transition. Slicing (`SLICE_COUNT`/`SLICE_ID`) and checkpoint/resume let a long run be split across machines or restarted.

### Compile-time parameters

| `#define`  | Meaning |
|---|---|
| `N` | dimension to search |
| `PREFIX_LENGTH`| prefix depth = scheduling granularity |
| `SLICE_COUNT` / `SLICE_ID` | split into `SLICE_COUNT` independent runs; this one computes slice `SLICE_ID` |

### Building by hand

```bash
cd exhaustive/dfs_search
make clean && make DEFS="-DN=8 -DPREFIX_LENGTH=18"   # omit DEFS for the N=6 defaults
cd ..                                                 # run from exhaustive/
mpirun --oversubscribe -n 5 dfs_search/dfs_search
```

> **Always `make clean` when changing `DEFS`.** The build does not track them, so a new `-DN=` otherwise reuses objects from the old dimension. `run_exhaustive.sh` does this for you.

---

## Heuristic — `heuristic/pruned_bfs_search/`

The foundations of heuristic pruned breadth-first search are given can be found in Thomas E. Ace's *New Lower Bounds for Snake-in-the-Box in 11-, 12-, and 13-dimensional Hypercubes* and the core algorithm is a direct C translation of the Python `snake_in_box` package by Daniel Ari Friedman ([docxology/snake](https://github.com/docxology/snake)). The track contains five algorithms: two that search a dimension directly, and three that extend an existing snake into a higher one.

| Algorithm | Function |
|---|---|
| `snake_in_box` | direct search |
| `parallel_search` | direct search, OpenMP parallelism |
| `priming` | extend a seed one dimension at a time up to the target |
| `extend_snake` | extend a seed straight to the target dimension |
| `parallel_extend` | like `extend_snake`, with OpenMP per-level parallelism |

### Parameters

All take `<dimension> <memory_gb>`; the seeded tools take seed files after that.

| Argument | Meaning |
|---|---|
| `dimension` | dimension to extend into |
| `memory_gb` | memory budget per bfs level; the search prunes stored snake paths to fit within the budget |
| `workers` | `parallel_search` / `parallel_extend` only — OpenMP thread count |
| `seed files` | `priming` / `extend_snake` / `parallel_extend` only — a `.txt` of a transition sequence |

---

## Snakes, seeds, and file formats

### Transition sequences

A snake can be stored as a **transition sequence**: a sequence of integer bit positions that flip between consecutive vertices, i.e. $\log_2(v_i \oplus v_{i+1})$. Decode by starting at vertex `0` and XOR-ing in each flipped bit:

```
transitions:  0 1 2 3 0 1 4 0 2 1 0 3 2
vertices:     0 1 3 7 15 14 12 28 29 25 27 26 18 22
```

A snake of length $L$ has $L$ edges and $L+1$ vertices.

> **Note the two length conventions.** Length is in **edges** everywhere except the exhaustive track's `.bin` filenames, which count **vertices** ($=$ edges $+ 1$). The 26-edge snake in $Q_6$ is `6D_L27_rank2.bin`.

### Output

After running the search algorithms, their corresponding outputs will appear in these folders:

| Directory | Contents |
|---|---|
| `heuristic/seeds/` | integer-only transition sequences |
| `heuristic/snakes/` | snake length, transition, and vertex sequences |
| `exhaustive/job_outputs/` | search summaries and runtimes |
| `exhaustive/job_outputs/snakes_dfs_search/` | `.bin` files of longest snakes |

## Prerequisites

| Prerequisite | Used by |
|---|---|
| C/C++ compiler | both tracks |
| Make | both tracks |
| OpenMPI | exhaustive track |
| `libomp` | heuristic `parallel_search`, `parallel_extend` (macOS only) |

---

## Project structure

```text
snake-in-the-box/
├── run_exhaustive.sh              # build + run the exhaustive track
├── run_heuristic.sh               # build + run the heuristic track
├── exhaustive/
│   ├── dfs_search/                # C++ sources only (config.hpp, drivers, …)
│   ├── slurm/                     # SLURM batch scripts
│   └── job_outputs/
└── heuristic/
    ├── pruned_bfs_search/         # C sources only
    ├── slurm/                     # SLURM batch scripts
    ├── seeds/                     # transition sequences (solver output)
    └── snakes/                    # human-readable snake records
```

## License

MIT — see [LICENSE](LICENSE).

## Acknowledgments

* **Exhaustive search:** inspired by the algorithm developed by Ekaterina Simakova in
  [kat-devs/final-year-project](https://github.com/kat-devs/final-year-project) (no
  code reused — the implementation here is original).
* **Heuristic search:** a direct C translation of the Python `snake_in_box` package by
  Daniel Ari Friedman ([docxology/snake](https://github.com/docxology/snake), MIT) —
  see [LICENSE](LICENSE) for the full attribution notice.

Development of this repository was assisted by AI coding tools. All generated snakes
are given as transition sequences that can be independently verified.