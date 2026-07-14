# dfs_search on Windows (MS-MPI)

A Windows-buildable copy of `dfs_search_streaming_slices/`. The algorithm and MPI
usage are identical; only three portability points differ from the mac/Linux
sources, all fixed here:

| issue | mac/Linux | Windows fix (in this copy) |
|---|---|---|
| count-trailing-zeros | `__builtin_ctz` (GCC/Clang only) | `sib_ctz()` in `config.hpp` → `_BitScanForward` under MSVC |
| checkpoint atomic swap | `rename()` replaces atomically | `remove()` then `rename()` under `_WIN32` (`driver_main.cpp`) |
| build tool | `mpicxx` (Makefile) | `cl.exe` + MS-MPI SDK via `build.bat` |

The MPI calls used (`Init`, `Comm_rank/size`, `Send`, `Recv`, `Bcast`,
`Allreduce`, `Wtime`, `Finalize`) are all standard and fully supported by MS-MPI.

## Prerequisites

1. **Visual Studio** with the "Desktop development with C++" workload (gives `cl.exe`).
2. **Microsoft MPI** (runtime) **and the MS-MPI SDK** — install both from
   Microsoft's MPI page. The SDK sets the environment variables `MSMPI_INC` and
   `MSMPI_LIB64` that `build.bat` reads.

## Build

Open an **"x64 Native Tools Command Prompt for VS"** (so `cl.exe` and the env
vars are on PATH), `cd` into this `windows\` folder, then:

```bat
build.bat
```

That produces `dfs_search.exe` (dispatcher) and `dfs_search_replay.exe` (worker-side
replay). Override the compile-time knobs with `/D` flags (defaults: N=6,
PREFIX_LENGTH=11, one slice = full search):

```bat
build.bat /DN=6 /DPREFIX_LENGTH=11 /DSLICE_COUNT=4 /DSLICE_ID=0
```

| knob | meaning |
|---|---|
| `/DN=`            | hypercube dimension |
| `/DPREFIX_LENGTH=`| prefix depth (scheduling granularity) |
| `/DSLICE_COUNT=`  | number of stripes to split the search into |
| `/DSLICE_ID=`     | which stripe this run computes (`0 .. SLICE_COUNT-1`) |
| `/DROOT_OFFSET=` `/DROOT_COUNT=` | window mode (when `SLICE_COUNT==1`) |

As on mac/Linux, each distinct slice needs its own build (the knobs are
compile-time `#define`s). Cover the whole search by running every `SLICE_ID`.

## Run

```bat
mpiexec -n 5 dfs_search.exe
mpiexec -n 5 dfs_search_replay.exe
```

`-n` is the process count (`1` dispatcher + workers for `dfs_search`; all ranks
search for `dfs_search_replay`). Use `-n` ≈ physical cores. Decode emitted snakes:

```bat
dfs_search.exe --decode job_outputs\snakes_dfs_search\6D_L27_rank1.bin
```

Expected (same ground truth as mac/Linux): D=6 → 26 edges, count 1; D=5 → 13
edges, 8 snakes.

## Notes

- The in-place progress bar uses ANSI escape codes; Windows 10/11 Terminal
  renders them, older `cmd.exe` may show stray characters (cosmetic only).
- MinGW-w64 `g++` also works if you prefer it (it has `__builtin_ctz`, so the
  MSVC branch is unused): compile/link against the MS-MPI import lib instead of
  using `build.bat`.
