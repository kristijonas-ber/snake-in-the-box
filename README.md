# Snake-in-the-Box

The snake-in-the-box problem wants to find the longest *snake*—an induced path with no chords—in an
$n$-dimensional hypercube $Q_n$. There are two approaches to this problem.

| Track | Method | Result | Reach |
|---|---|---|---|
| [`exhaustive/`](exhaustive/) | parallelized depth-first search with automorphism rejection | enumerates every longest snake | feasible up to dimension $7$ |
| [`heuristic/`](heuristic/) | fitness-pruned breadth-first search | finds maximal snakes only | no limit |

---

## Overview

The exhaustive track has proven the exact maximum for every dimension up to $7$, along with the number of canonical longest snakes.

| Dimension | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| **Longest snake (edges)** | 1 | 2 | 4 | 7 | 13 | 26 | 50 | 98 |
| **Canonical longest snakes** | 1 | 1 | 1 | 1 | 8 | 1 | 12 | N/A |

Beyond that the maximum is unknown, so the heuristic track chases lower bounds; the longest snakes it has seeds for are these, each credited to its discoverer.

| Dimension | 9 | 10 | 11 | 12 | 13 |
|---|---|---|---|---|---|
| **Edges** | 190 | 370 | 732 | 1439 | 2854 |
| **Due to** | Wynn | Kinny | Ace | Ace | Ace |

---

## Quick start

Two scripts at the repo root build and run either track.

```bash
./run_exhaustive.sh --procs 5 --oversubscribe   # dimension 6 → 26 edges, count 1
./run_heuristic.sh serial 7 18.0                # dimension 7, 18 GB memory budget
```

**Heuristic** — the mode picks the algorithm; then `<dimension> <memory_gb>` and,
for the extenders, a seed file:

| Command | What it does |
|---|---|
| `./run_heuristic.sh serial 7 18.0` | search $Q_7$ for a snake, capped at 18 GB |
| `./run_heuristic.sh parallel 7 18.0 10` | same search, spread over 10 CPU threads |
| `./run_heuristic.sh priming 8 18.0 seed.txt` | grow `seed.txt` up to $Q_8$, one dimension per step |
| `./run_heuristic.sh extend 8 18.0 --both-ends seed.txt` | grow `seed.txt` straight into $Q_8$, from both ends |

**Exhaustive** — proves the maximum for a dimension; flags tune the run:

| Command | What it does |
|---|---|
| `./run_exhaustive.sh --dim 7 --procs 10` | find every longest snake in $Q_7$ using 10 processes |
| `./run_exhaustive.sh --dim 8 --slice-count 64 --slice-id 0` | compute just slice 0 of 64 — run the rest elsewhere, in any order |
| `./run_exhaustive.sh --dim 8 --prefix-length 18` | deeper prefixes = finer-grained work units for the ranks |
| `./run_exhaustive.sh --decode <file>.bin` | print the snakes stored in a result file |

`--help` on either script lists every option.

---

## Exhaustive — `exhaustive/dfs_search/`

The foundations of exhaustive snake-in-the-box algorithms can be found in Ville Pettersson's doctoral dissertation, *Graph Algorithms for Constructing and Enumerating Cycles and Related Structures* (Aalto University publication series, Doctoral Dissertations 127/2015), which develops the canonical-augmentation search with isomorph rejection and applies it to prove $s(8) = 98$ for snakes and $c(8) = 96$ for coils.

The table below lists these and additional techniques for optimization.

### What makes it tractable

| Technique | Effect |
|---|---|
| **Canonical augmentation** | a snake may only introduce dimension $k$ once $0…k−1$ are used, so of each automorphism group only one representative is ever explored |
| **Incremental chord test** | each vertex keeps a running count of its snake-neighbours, adjusted in $O(N)$ as the path grows and shrinks; a count of 2 forbids the vertex, so a move's legality is checked without ever rescanning the path |
| **Branch and bound** | a running count of still-usable vertices prunes any branch where `length + available < target` |
| **Streaming prefix generation** | prefixes are emitted by ordinal from a deterministic walk, so no prefix table is ever held in RAM |
| **Two passes** | the first finds the longest length *L*, the second re-walks and emits only the snakes of that length |

Snakes are written as one byte per transition; slicing (`SLICE_COUNT`/`SLICE_ID`) and
checkpoint/resume let a long run be split across machines or restarted.

### Compile-time parameters

| `#define`  | Meaning |
|---|---|
| `N` | dimension to search |
| `PREFIX_LENGTH`| prefix depth = scheduling granularity |
| `SLICE_COUNT` / `SLICE_ID` | split into `SLICE_COUNT` independent runs; this one computes slice `SLICE_ID` |

Window mode, checkpoint/resume and Knuth probes are in
[`config.hpp`](exhaustive/dfs_search/config.hpp).

### Building by hand

```bash
cd exhaustive/dfs_search
make clean && make DEFS="-DN=8 -DPREFIX_LENGTH=18"   # omit DEFS for the N=6 defaults
cd ..                                                 # run from exhaustive/
mpirun --oversubscribe -n 5 dfs_search/dfs_search
```

> **Always `make clean` when changing `DEFS`.** The build does not track them, so a
> new `-DN=` otherwise reuses objects from the old dimension. `run_exhaustive.sh`
> does this for you.

---

## Heuristic — `heuristic/pruned_bfs_search/`

The track contains four algorithms: two that search a dimension directly, and two that extend an existing snake into a higher one.

| Algorithm | Function |
|---|---|
| `snake_in_box` | direct search |
| `parallel_search` | direct search, OpenMP parallelism |
| `priming` | extend a seed one dimension at a time up to the target |
| `extend_snake` | extend a seed straight to the target dimension |

### Parameters

All four take `<dimension> <memory_gb>`; the seeded tools take seed files after that.

| Argument | Meaning |
|---|---|
| `dimension` | dimension to search, or to extend *into* |
| `memory_gb` | memory budget; the search prunes once it exceeds this |
| `workers` | `parallel_search` only — OpenMP thread count |
| `seed files` | `priming` / `extend_snake` only — a `.txt` of transition integers, or a `.bin` from the exhaustive track |

`extend_snake` also takes `--both-ends` (grow each seed from its other endpoint too)
and any number of seed files at once.

### Building by hand

```bash
cd heuristic/pruned_bfs_search
make                  # ./snake_in_box
make parallel_search  # macOS: OMPFLAGS="-Xpreprocessor -fopenmp -lomp"
make priming
make extend_snake
cd ..                 # run from heuristic/
pruned_bfs_search/snake_in_box 7 18.0
```

---

## Snakes, seeds, and file formats

### Transition sequences

A snake can be stored as a **transition sequence**: a sequence of integer bit positions that flip between
consecutive vertices, i.e. $\log_2(v_i \oplus v_{i+1})$. Decode by starting at vertex
`0` and XOR-ing in each flipped bit:

```
transitions:  0 1 2 3 0 1 4 0 2 1 0 3 2
vertices:     0 1 3 7 15 14 12 28 29 25 27 26 18 22
```

A snake of length $L$ has $L$ edges and $L+1$ vertices.

> **Note the two length conventions.** Length is in **edges** everywhere except the exhaustive
> track's `.bin` filenames, which count **vertices** ($=$ edges $+ 1$). The 26-edge
> snake in $Q_6$ is `6D_L27_rank2.bin`.

### Output

| Directory | Contents |
|---|---|
| `heuristic/seeds/` | integer-only transition sequences |
| `heuristic/snakes/` | snake length, transition, and vertex sequences |
| `exhaustive/job_outputs/` | search summaries and runtimes |
| `exhaustive/job_outputs/snakes_dfs_search/` | `.bin` files of longest snakes |


### File naming

```
dim<N>_len<len>[_<surname>].txt
```

`<len>` is in **edges**. Solvers save `dim<N>_len<len>.txt` into both `seeds/` and
`snakes/`. The trailing `_<surname>`
credits the discoverer and is added by hand.

| Example | Meaning |
|---|---|
| `dim15_len10149.txt` | dimension 15, length 10149 |
| `dim13_len2854_ace.txt` | discovered by Ace |

---

## Prerequisites & platforms

The top-level sources target **macOS and Linux**; ports for Windows and the SLURM
manager are provided as well.

| Prerequisite | Used by |
|---|---|
| C/C++ compiler | both tracks |
| Make | both tracks |
| OpenMPI | exhaustive track |
| `libomp` | heuristic `parallel_search` |

| Platform | Path |
|---|---|
| **macOS / Linux** | `heuristic/pruned_bfs_search/` or `exhaustive/dfs_search/` |
| **Windows** | `heuristic/pruned_bfs_search/windows/` or `exhaustive/dfs_search/windows/` |
| **HPC (SLURM)** | `heuristic/pruned_bfs_search/slurm/` or `exhaustive/dfs_search/slurm/` |

---

## Project structure

```text
snake-in-the-box/
├── run_exhaustive.sh              # build + run the exhaustive track
├── run_heuristic.sh               # build + run the heuristic track
├── exhaustive/
│   ├── dfs_search/
│   │   ├── config.hpp
│   │   ├── slurm/                 # SLURM batch scripts
│   │   └── windows/               # MSVC + MS-MPI port
│   └── job_outputs/
└── heuristic/
    ├── pruned_bfs_search/
    │   ├── slurm/                 # SLURM batch scripts
    │   └── windows/               # MSVC + MS-MPI port
    ├── seeds/
    └── snakes/
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

Development of this repository was assisted by AI coding tools. All reported snakes
are given as transition sequences that can be independently verified.