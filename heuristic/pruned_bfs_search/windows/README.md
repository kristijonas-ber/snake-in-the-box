# Snake-in-the-Box — C Translation

This folder is a C translation of the core **algorithm** modules of the Python
`snake_in_box` package. It was produced by translating the Python source
file-by-file into C, **preserving the original logic, structure, function names,
and docstrings** (rephrased as C comments). It is not a rewrite or an
optimization — the behavior matches the Python implementation.

The "snake-in-the-box" problem asks for the longest induced path (a *snake*) in
an n-dimensional hypercube graph Q_n. A snake is encoded as a *transition
sequence*: the list of bit positions that flip between consecutive vertices.

## Which files were translated, and why

Only the files that contain actual **algorithms / data structures** were
translated. Visualization, reporting, plotting, parallel orchestration,
benchmark data tables, and test/script glue were intentionally skipped — they
are either I/O, plotting, or Python-runtime-specific code rather than algorithms.

| C file (`.h`/`.c`)    | Translated from (Python)                | What it is |
|-----------------------|------------------------------------------|------------|
| `hypercube`           | `core/hypercube.py`                       | `HypercubeBitmap`: 64-bit-word bitmap tracking which hypercube vertices are marked (occupied/prohibited). |
| `transitions`         | `core/transitions.py`                     | Conversions between vertex sequences, transition sequences, and the paper's hex-string format. |
| `validation`          | `core/validation.py`                      | Hamming distance and snake validity checks (consecutive distance 1, non-consecutive distance > 1). |
| `canonical`           | `utils/canonical.py`                      | Kochut canonical-form test and legal-next-dimension enumeration for symmetry reduction. |
| `snake_node`          | `core/snake_node.py`                      | `SnakeNode`: a search-tree node holding a path, its vertex bitmap, and a fitness score. |
| `fitness`             | `search/fitness.py`                       | Fitness measures: unmarked-vertex count (simple), dead ends, and flood-fill unreachable count. |
| `bfs_pruned`          | `search/bfs_pruned.py`                    | The heuristically-pruned breadth-first search from Ace (2025) — the main search algorithm. |
| `parallel_search.c`   | `search/parallel.py`                      | Per-level parallel node expansion. Splits each level into per-worker chunks (OpenMP threads in place of `multiprocessing.Pool`), merges the children, and folds each worker's best snake into the global best under a critical section. |
| `priming.c`           | `search/priming.py` (`prime_search`)      | Incremental priming: extend a seed snake **one dimension at a time** up to a target dimension, re-seeding each step with the previous best. |
| `extend_snake.c`      | `search/priming.py` (single jump)         | Seed the beam with an existing snake and extend it straight into one higher target dimension. |
| `main.c`              | (demo)                                    | A small driver: run a search, validate, and print the result. |

### Files deliberately not translated

- `core/calculation.py` — strategy selector (known snake vs. search vs.
  priming); orchestration, not an algorithm.
- `benchmarks/known_snakes.py` — a data table of record hex strings (parse it
  with `parse_hex_transition_string`).
- `analysis/`, `utils/visualize*`, `utils/export`, `utils/performance_plots`,
  `reporting`, `tests/`, `scripts/` — analysis, plotting, I/O, and tests.

## Building and running

```sh
make            # builds ./snake_in_box
make run        # runs the demo for dimension 7
./snake_in_box 7 18.0   # dimension 7, 18 GB memory limit
make clean
```

### Parallel search (`parallel.py`)

```sh
make parallel_search
./parallel_search 7 18.0 10   # dimension 7, 18 GB, 10 workers
```

Parallelism uses OpenMP. On GCC/Linux the default `-fopenmp` flag works as-is.
The code also builds and runs correctly *without* OpenMP (falls back to a single
thread) — just compile `parallel_search.c` without the flag. On macOS with Apple
Clang, install libomp and override the flag, e.g.:

```sh
make parallel_search OMPFLAGS="-Xpreprocessor -fopenmp -lomp \
    -I$(brew --prefix libomp)/include -L$(brew --prefix libomp)/lib"
```

### Seeding / priming (`priming.py`)

Both seeded tools read a seed snake as space/newline-separated transition
integers from a file (default `extend_input.txt`) — paste a snake exactly as the
drivers print it after `Transitions:`.

```sh
make priming
./priming 8 18.0 extend_input.txt        # step the seed up to dimension 8

make extend_snake
./extend_snake 8 18.0 extend_input.txt    # jump the seed straight to dimension 8
```

`priming` mirrors `prime_search`: it steps one dimension at a time and stops
early (returning the best snake found so far) if a step cannot extend.

### Building on Windows (MSVC / Visual Studio)

This folder is the Windows-portable copy of the C translation. The
GCC/Clang-only `__builtin_*` bit intrinsics are wrapped in `bitops.h`, which maps
them to the MSVC `<intrin.h>` equivalents, so the sources build with `cl.exe`.

Open an **"x64 Native Tools Command Prompt for VS"** (needed so `cl` targets x64,
which `_BitScanForward64` in `bitops.h` requires) and run the build script:

```bat
cd pruned_bfs_c_windows\windows
build.bat
cd ..
parallel_search.exe 7 18.0 10
priming.exe 8 18.0 extend_input.txt
snake_in_box.exe 7
```

`build.bat` compiles all four executables, passing `/openmp` for
`parallel_search.exe`. MSVC's `/openmp` (OpenMP 2.0) covers every construct the
parallel search uses.

#### Chained cross-dimension extension (`chain_extend.bat`)

`chain_extend.bat` automates a multi-dimension climb: it grows a seed one
dimension at a time from `start_dim` to `end_dim`, feeding each step's result in
as the next step's seed, under a fixed per-step RAM budget (what the beam prunes
against). Unlike `priming.exe` — which chains internally but saves only the final
snake — this runs `extend_snake.exe` per dimension, so **every** dimension's
result is saved.

```bat
cd pruned_bfs_search\windows
chain_extend.bat dim13_seed.txt 13 20 64 --both-ends
```

Results land under `chain_out\dim<D>\{seeds,snakes}\` for each `D` from
`start_dim+1` to `end_dim`; the `seeds\` copy of one step is the reusable input to
the next. Any flags after the RAM budget (e.g. `--both-ends`) pass straight
through to `extend_snake.exe`. Override the output root by setting `CHAIN_OUT`.

**MinGW-w64 / Clang on Windows** don't need the build script — they support the
`__builtin_*` path and `-fopenmp`, so the GNU `Makefile` above works unchanged.

Requires a C11 compiler. On GCC/Clang the popcount/ctz helpers compile to the
standard `__builtin_*` intrinsics; on MSVC they use `<intrin.h>` plus a software
popcount fallback (see `bitops.h`).

## Notes on the translation

The translation is faithful, so it reproduces the Python results exactly —
including the fact that the bundled direct `pruned_bfs_search` is a relatively
weak greedy/canonical search. For example, both the Python and this C version
return length 5 for Q_5 and length 7 for Q_7 from the direct search. The
record-length snakes in the original project come from the *known-snakes
database* and the *priming* strategy, not from this direct search routine.

Idiomatic C adaptations that do not change behavior:

- **No exceptions.** Python raised `ValueError`/`IndexError`; here functions
  return `bool`/`-1` and print a matching message to `stderr`.
- **Explicit memory ownership.** Python lists/objects become heap-allocated
  arrays and `struct`s. Every `*_init` has a matching `*_free`; search levels
  free dropped nodes during pruning. Sequences are passed as `int*` + length.
- **Tuple returns.** `validate_*` returned `(bool, message)`; here the `bool` is
  the return value and the message goes into a caller-provided buffer.
- **`set`/`dict`.** Used-dimension sets become small boolean/presence arrays;
  the flood fill uses a visited bitmap plus an array-backed queue.
- **Popcount.** `bin(x).count('1')` becomes `__builtin_popcountll`.

Each `.c`/`.h` file names the exact Python source it came from at the top.
