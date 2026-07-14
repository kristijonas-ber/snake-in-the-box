# Snake-in-the-Box

Finding the longest *snake* — an induced path with no shortcuts — in an
$n$-dimensional hypercube graph $Q_n$.

| Track | Method | Guarantee | Reach |
|---|---|---|---|
| [`exhaustive/`](exhaustive/) | canonical-augmentation DFS over MPI | proves the maximum, enumerates every longest snake | $Q_6$ on one machine |
| [`heuristic/`](heuristic/) | fitness-pruned BFS beam search | lower bound only | $Q_{15}$ in minutes |

---

## Results

**Exhaustive — proven optimal.**

| Dimension | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| **Longest snake (edges)** | 1 | 2 | 4 | 7 | 13 | 26 |
| **Distinct longest snakes** | 1 | 1 | 1 | 1 | 8 | 1 |

The proven optima continue 7 → 50 and 8 → 98, both out of reach here on one machine.

**Heuristic — lower bounds.** Grown by this project, by extending the seeds below:

| Dimension | Longest snake (edges) | File |
|---|---|---|
| 14 | 5457 | `heuristic/seeds/dim14_len5457_bernatonis.txt` |
| 15 | 10375 | `heuristic/seeds/dim15_len10375_bernatonis.txt` |

The seeds they grew from are **prior work, not results of this repo**:

| Dimension | 9 | 10 | 11 | 12 | 13 |
|---|---|---|---|---|---|
| **Edges** | 190 | 370 | 732 | 1439 | 2854 |
| **Due to** | Wynn | Kinny | Ace | Ace | Ace |

---

## Quick start

Two scripts at the repo root build and run either track. They build with the right
flags and launch from the right directory, so output lands where it belongs.

```bash
./run_exhaustive.sh --procs 5 --oversubscribe   # dimension 6 → 26 edges, count 1
./run_heuristic.sh serial 7 18.0                # dimension 7, 18 GB memory budget
```

`run_heuristic.sh <mode> [args...]` — everything after the mode reaches the binary
unchanged:

| Command | Runs |
|---|---|
| `./run_heuristic.sh serial 7 18.0` | `snake_in_box` — direct search |
| `./run_heuristic.sh parallel 7 18.0 10` | `parallel_search` — OpenMP, 10 threads |
| `./run_heuristic.sh priming 8 18.0 seed.txt` | `priming` — extend a seed one dimension at a time |
| `./run_heuristic.sh extend 8 18.0 --both-ends seed.txt` | `extend_snake` — extend a seed straight to dimension 8 |

`run_exhaustive.sh` rebuilds with the right compile-time defines, then runs under
`mpirun`:

| Command | Runs |
|---|---|
| `./run_exhaustive.sh --dim 8 --prefix-length 18 --procs 16` | dimension 8, prefix depth 18, 16 MPI ranks |
| `./run_exhaustive.sh --dim 8 --slice-count 64 --slice-id 0 --replay` | slice 0 of 64, dispatcher-free build |
| `./run_exhaustive.sh -D KNUTH_PROBES=1000 -D PROBE_ONLY=1` | any other `config.hpp` knob |
| `./run_exhaustive.sh --decode <file>.bin` | print the snakes in a result file |

`--help` on either script lists every option.

---

## Exhaustive — `exhaustive/dfs_search/`

Two binaries, same result: `dfs_search` (rank 0 dispatches prefixes dynamically;
needs **≥2 ranks**) and `dfs_search_replay` (no dispatcher; every rank statically
owns its share).

### What makes it tractable

| Technique | Effect |
|---|---|
| **Canonical augmentation** | a snake may only introduce dimension *k* once 0…*k*−1 are used, so of each symmetric family only one representative is ever explored |
| **Incremental chord test** | a per-vertex count of snake-neighbours, updated on push/pop; reaching 2 marks the vertex forbidden, so validity is $O(N)$ per move and never rescanned |
| **Branch and bound** | a running count of still-usable vertices prunes any branch where `length + available < target` |
| **Streaming prefix generation** | prefixes are emitted by ordinal from a deterministic walk, so no prefix table is ever held in RAM |
| **Two passes** | the first finds the longest length *L*, the second re-walks and emits only the snakes of that length — nothing is buffered |
| **Knuth probes** | random-path sampling estimates the size of the tree without searching it (`KNUTH_PROBES`, `PROBE_ONLY`) |

Snakes are written as one byte per transition; slicing (`SLICE_COUNT`/`SLICE_ID`) and
checkpoint/resume let a long run be split across machines or restarted.

### Parameters — compile-time

`#define`s, not runtime arguments, so **changing one means rebuilding**.

| `#define` | Default | Meaning |
|---|---|---|
| `N` | 6 | dimension to search |
| `PREFIX_LENGTH` | 11 | prefix depth = scheduling granularity |
| `SLICE_COUNT` / `SLICE_ID` | 1 / 0 | split into `SLICE_COUNT` independent runs; this one computes slice `SLICE_ID` |

Window mode, checkpoint/resume and Knuth probes are in
[`config.hpp`](exhaustive/dfs_search/config.hpp).

### Building by hand

```bash
cd exhaustive/dfs_search
make clean && make DEFS="-DN=8 -DPREFIX_LENGTH=18"   # omit DEFS for the N=6 defaults
cd ..                                                 # run from exhaustive/
mpirun --oversubscribe -n 5 dfs_search/dfs_search
```

> **Always `make clean` first.** The object rule does not depend on `DEFS`, so a
> changed `-DN=` silently relinks stale objects built for the old dimension.
> `run_exhaustive.sh` handles this.

---

## Heuristic — `heuristic/pruned_bfs_search/`

| Binary | Does |
|---|---|
| `snake_in_box` | direct search |
| `parallel_search` | direct search, OpenMP per-level parallelism |
| `priming` | extend a seed one dimension at a time up to the target |
| `extend_snake` | extend a seed straight to the target dimension |

### Parameters — runtime

All four take `<dimension> <memory_gb>`; the seeded tools take seed files after that.

| Arg | Meaning |
|---|---|
| `dimension` | dimension to search, or to extend *into* |
| `memory_gb` | memory budget; the search prunes once it exceeds this |
| `workers` | `parallel_search` only — OpenMP thread count |
| `seed file(s)` | `priming` / `extend_snake` only — a `.txt` of transition integers, or a `.bin` from the exhaustive track |

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

A snake is stored as a **transition sequence**: the bit positions that flip between
consecutive vertices, i.e. $\log_2(v_i \oplus v_{i+1})$. Decode by starting at vertex
`0` and XOR-ing in each flipped bit:

```
transitions:  0 1 2 3 0 1 4 0 2 1 0 3 2        (a length-13 snake in Q_5)
vertices:     0 1 3 7 15 14 12 28 29 25 27 26 18 22
```

A length-$L$ snake has $L$ transitions and $L+1$ vertices. Both solvers print this
after `Transitions:`, so every reported snake can be re-checked independently.

> **Two length conventions.** Length is in **edges** everywhere — this README, both
> solvers' output, every `dim<N>_len<len>` filename — except the exhaustive track's
> `.bin` filenames, which count **vertices**. The 26-edge snake in $Q_6$ is
> `6D_L27_rank2.bin`.

### Where output lands

| Directory | Contents |
|---|---|
| `heuristic/seeds/` | bare transition sequences — **integers only**, reloadable as a seed |
| `heuristic/snakes/` | the same snakes, human-readable (transitions, vertices, validation) |
| `exhaustive/job_outputs/` | text summaries and per-rank fragments |
| `exhaustive/job_outputs/snakes_dfs_search/` | `.bin` files of every longest snake; read with `./run_exhaustive.sh --decode <file>.bin` (gitignored — a fresh clone has none) |

Both solvers write these paths **relative to the current working directory**, so they
must be launched from the track root (`heuristic/` or `exhaustive/`). The runner
scripts do this.

### File naming

```
dim<N>_len<len>[_<surname>].txt
```

`<len>` is in **edges**. Solvers save `dim<N>_len<len>.txt` into both `seeds/` and
`snakes/`, adding `_2`, `_3`, … if the name is taken. The trailing **`_<surname>`
credits the discoverer and is added by hand, never by the code**.

| Example | Meaning |
|---|---|
| `dim15_len10149.txt` | a solver's output, unattributed |
| `dim15_len10375_bernatonis.txt` | this project's record for $Q_{15}$ |
| `dim13_len2854_ace.txt` | prior work by Ace — an input, not a result of this repo |

### Adding a seed

One snake per file, nothing but space- or newline-separated transition integers —
any stray text and the file is rejected. Every value must be less than the dimension
you extend into. From **vertex numbers**, convert each step with
$\log_2(v_i \oplus v_{i-1})$; from a **hex string**, each digit is one transition
(`0`–`9`, `a`–`f` = 10–15).

```bash
# all seeds at once, growing each from both ends
./run_heuristic.sh extend 8 18.0 --both-ends heuristic/seeds/*.txt

# .bin files from the exhaustive track work as seeds too
./run_heuristic.sh extend 8 18.0 --both-ends exhaustive/job_outputs/snakes_dfs_search/6D_L27_rank*.bin
```

---

## Prerequisites & platforms

The top-level sources target **macOS and Linux**: a C/C++ compiler (GCC or Clang),
Make, OpenMPI (exhaustive only), and on macOS `libomp` for the OpenMP heuristic
search (`brew install libomp` — `run_heuristic.sh` passes the flags automatically).

| Platform | Where |
|---|---|
| **macOS / Linux** | the default sources in each track |
| **Windows** | [`exhaustive/dfs_search/windows/`](exhaustive/dfs_search/windows/README_WINDOWS.md) (MSVC + MS-MPI) and [`heuristic/pruned_bfs_search/windows/`](heuristic/pruned_bfs_search/windows/README.md) |
| **HPC (SLURM)** | each track's `slurm/` folder — see [`CLAUDE.md`](CLAUDE.md) for the UCD Sonic walkthrough |

---

## Structure

```text
snake-in-the-box/
├── run_exhaustive.sh              # build + run the exhaustive track
├── run_heuristic.sh               # build + run the heuristic track
├── exhaustive/
│   ├── dfs_search/                # canonical-augmentation DFS over MPI
│   │   ├── config.hpp             # compile-time search knobs
│   │   ├── slurm/                 # SLURM batch scripts
│   │   └── windows/               # MSVC + MS-MPI port
│   └── job_outputs/               # text summaries + .bin snake files
└── heuristic/
    ├── pruned_bfs_search/         # fitness-pruned BFS beam search
    │   ├── slurm/                 # SLURM batch scripts
    │   └── windows/               # Windows port
    ├── seeds/                     # transition sequences (reloadable as seeds)
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

Development of this repository was assisted by AI coding tools. All reported snakes
are given as transition sequences that can be independently verified.
