# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Working style
- **Do not run scripts, benchmarks, or test runs yourself.** Tell the user the exact
  command(s) to run and let them run it. The one exception: when you genuinely need to
  convince yourself the code does not crash / compiles, a minimal build or smoke-run is
  fine — but keep it minimal and prefer handing the user the command otherwise.

## Repository layout

This repo (remote: `github.com/kristijonas-ber/snake-in-the-box`) holds the
Snake-in-the-Box project: exhaustive and heuristic search for the
longest induced path (a *snake*) in an n-dimensional hypercube.

Start with [`README.md`](README.md) for the project overview. Two search tracks
live at the top level:

- **Exhaustive** — [`exhaustive/dfs_search/`](exhaustive/dfs_search/README.md):
  canonical-augmentation DFS over MPI. Search knobs (`N`, `PREFIX_LENGTH`,
  `SLICE_COUNT`/`SLICE_ID`, …) are **compile-time** `-D` defines; see
  [`config.hpp`](exhaustive/dfs_search/config.hpp).
- **Heuristic** — [`heuristic/pruned_bfs_search/`](heuristic/pruned_bfs_search/README.md):
  fitness-pruned BFS beam search. Four binaries (`snake_in_box`,
  `parallel_search`, `priming`, `extend_snake`) taking **runtime** args.

Each track's `README.md` is authoritative for what its code does — don't
duplicate that here. Operational/HPC detail lives in this file (below).

Both `exhaustive/history/` and `heuristic/history/` hold superseded earlier
implementations, kept for reference and gitignored. `plots/` is likewise
gitignored.

## Runner scripts

Two root-level wrappers build and run anything in either track from one place:

```bash
./run_heuristic.sh <serial|parallel|priming|extend> [args...]   # args passed through verbatim
./run_exhaustive.sh [--dim N] [--procs P] [--replay] [-D KEY=VAL] [--decode FILE]
```

`run_exhaustive.sh` forces `make clean` before each build: the exhaustive
Makefile's object rule does not depend on `DEFS`, so a changed `-DN=` would
otherwise silently relink stale objects compiled for the old dimension.

## Running on Sonic HPC (UCD)

The heavy target is `parallel_search` — the OpenMP (shared-memory) heuristic
search. It runs on **one node** and scales across that node's cores. It does
**not** use MPI. The exhaustive track's `dfs_search` is the MPI one.

### 1. Get onto Sonic

```bash
ssh <ucd-username>@login.ucd.ie        # port 22; Windows: use PuTTY + WinSCP
```

Home is `/home/people/<username>` (~50 GB quota). Use `scratch/` (a symlink in
home) for large/temporary run output — but scratch is **not** long-term storage
(files untouched for 6 months are deleted).

Copy the project up with `scp`/`rsync` (or `git clone`):

```bash
rsync -av pruned_bfs_search/ <username>@login.ucd.ie:~/pruned_bfs_search/
```

### 2. Build

Sonic is Linux + GCC, so the code compiles unchanged. Do **not** build on the
login node — grab an interactive shell:

```bash
srun --time=00:30:00 -c4 --pty bash    # interactive worker shell, 4 cores
module load gcc                        # check versions with: module avail gcc
make parallel_search                   # OpenMP binary (plain -fopenmp works on GCC)
exit
```

`make` alone builds the serial demo; other targets: `make priming`,
`make extend_snake`.

### 3. Submit the parallel search job

The batch script is `sonic_parallel_search.sh`. Edit `DIMENSION`, `MEMORY_GB`,
core count (`-c`), walltime (`-t`), and `--mail-user` at the top, then:

```bash
sbatch sonic_parallel_search.sh        # returns: Submitted batch job <id>
```

Because `parallel_search` is OpenMP, the script requests **one node** (`-N 1`),
**one task** (`-n 1`), and **many cores** (`-c 16`), then sets
`OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK`. Do **not** use MPI-style multi-task /
multi-node settings for it.

### 4. Monitor / manage

```bash
squeue -u $USER          # queued/running jobs (id, name, state, time)
scancel <jobid>          # cancel a job
sacct -j <jobid>         # detailed accounting after it finishes
```

stdout/stderr land in `snake_parallel_<jobid>.out` (set by `-o` in the script).

### Sonic specifics that matter here

- **Cores / limits.** Shared users may use up to **48 cores** at once (up to 260
  when the cluster is quiet). Since this is single-node OpenMP, thread count is
  capped by one node's cores — keep `-c` sane (e.g. 16–32). More threads ≠
  speedup once the beam width is small.
- **Queues (`--partition`).** Default standard queue: max **10 days** walltime.
  CS-school users are routed to the `cs` queue. `dev` = short 1-hour test queue.
  GPU queues (`gpu`, `csgpu`) are irrelevant — this code is CPU-only.
- **High memory.** For very large dimensions add `--constraint=highmem` to reach
  the 1.5 TB / 2 TB RAM nodes. Set `MEMORY_GB` to match; the search prunes by
  fitness once it exceeds that limit.
- **Timing caveat.** `parallel_search.c` reports time via `clock()`, which sums
  CPU time **across threads** — printed seconds look inflated on many threads.
  For true wall-clock, switch those calls to `omp_get_wtime()` before collecting
  timing data.
- **Software.** `module avail` lists everything; `module load <pkg>` puts it on
  your PATH. You only need `gcc` here.
- **macOS note (local, not Sonic).** OpenMP needs
  `OMPFLAGS="-Xpreprocessor -fopenmp -lomp"`; `run_heuristic.sh` sets this
  automatically for `parallel` mode on Darwin.

### Quick reference

| Action | Command |
|--------|---------|
| Log in | `ssh <user>@login.ucd.ie` |
| Interactive shell (build) | `srun --time=00:30:00 -c4 --pty bash` |
| Build parallel binary | `make parallel_search` |
| Submit job | `sbatch sonic_parallel_search.sh` |
| Check jobs | `squeue -u $USER` |
| Cancel job | `scancel <jobid>` |
| Job accounting | `sacct -j <jobid>` |
| High-memory node | add `--constraint=highmem` |
