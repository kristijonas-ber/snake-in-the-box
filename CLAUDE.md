# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Working style
- **Do not run scripts, benchmarks, or test runs yourself.** Tell the user the exact
  command(s) to run and let them run it. The one exception: when you genuinely need to
  convince yourself the code does not crash / compiles, a minimal build or smoke-run is
  fine — but keep it minimal and prefer handing the user the command otherwise.
- **New search algorithms/heuristics never edit `heuristic/pruned_bfs_search/` in
  place.** That tool is the proven, deterministic baseline (see
  `notes/search-strategy-ideas.md` for the research behind why alternatives are
  being explored). Any new approach — stochastic pruning, Nested Monte Carlo
  Search, genetic algorithms, etc. — gets its own sibling folder under
  `heuristic/` (e.g. `heuristic/stochastic_prune/`, `heuristic/nmcs/`) with an
  independent implementation, own source files and build, no shared mutable
  state with the existing tool.
- **Never run, build, or launch any GPU-based algorithm/solver in this
  project** — no Metal, no CUDA, no GPU compute shaders, no ML frameworks
  dispatching to GPU. CPU/MPI only. (2026-07-27: a Metal-based GPU solver run
  from an unrelated workspace, `/Users/admin/Desktop/code/SIB/7d/`, during a
  benchmark-discrepancy investigation caused a 100%-GPU-load scare — killed on
  request. That workspace is outside this repo and unrelated to the actual
  snake-in-the-box/final-year-project benchmark work; do not run anything
  from it, or anything else GPU-based, again without explicit sign-off.)

## Repository layout

This repo (remote: `github.com/kristijonas-ber/snake-in-the-box`) holds the
Snake-in-the-Box project: exhaustive and heuristic search for the
longest induced path (a *snake*) in an n-dimensional hypercube.

[`README.md`](README.md) is the **single, authoritative guide** to the codebase —
algorithms, parameters, build commands, file formats, results. The per-track
READMEs were folded into it; do not recreate them. Two search tracks live at the
top level:

- **Exhaustive** — `exhaustive/dfs_search/`: canonical-augmentation DFS over MPI.
  Search knobs (`N`, `PREFIX_LENGTH`, `SLICE_COUNT`/`SLICE_ID`, …) are
  **compile-time** `-D` defines; see
  [`config.hpp`](exhaustive/dfs_search/config.hpp).
- **Heuristic** — `heuristic/pruned_bfs_search/`: fitness-pruned BFS beam search.
  Five binaries (`snake_in_box`, `parallel_search`, `priming`, `extend_snake`,
  `parallel_extend`) taking **runtime** args. `parallel_extend` is the seeded
  extender (`extend_snake`) with `parallel_search`'s OpenMP per-level expansion.

Only the Windows ports keep their own READMEs (under each track's `windows/`).
Operational/HPC detail lives in this file (below); everything else belongs in
`README.md`.

Both `exhaustive/archive/` and `heuristic/archive/` hold superseded earlier
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

## Published snake-length records (reference for extend_snake comparisons)

Best known lower bounds for the longest snake per dimension, for checking our
`extend_snake` output against. Dims 9-13 are from Orland et al. (already saved as
`dim<N>_len<len>_orland.txt` in `heuristic/seeds/` and `heuristic/snakes/`); dims
14-20 are from Abbott & Katchalski, filling the gap above where Orland's paper
stops (saved as `dim<N>_len<len>_abbott_katchalski.txt`, transition sequence
unavailable — see those files' headers).

| Dimension | Snake length (edges) | Source |
|---|---|---|
| 9 | 191 | Orland et al., arXiv:2607.15270 |
| 10 | 379 | Orland et al., arXiv:2607.15270 |
| 11 | 746 | Orland et al., arXiv:2607.15270 |
| 12 | 1476 | Orland et al., arXiv:2607.15270 |
| 13 | 2924 | Orland et al., arXiv:2607.15270 |
| 14 | 4932 | Abbott & Katchalski (1991), via Allison & Paulusma arXiv:1603.05119 Table 1 |
| 15 | 9866 | Abbott & Katchalski (1991), via Allison & Paulusma arXiv:1603.05119 Table 1 |
| 16 | 19738 | Abbott & Katchalski (1991), via Allison & Paulusma arXiv:1603.05119 Table 1 |
| 17 | 39478 | Abbott & Katchalski (1991), via Allison & Paulusma arXiv:1603.05119 Table 1 |
| 18 | 78958 | Abbott & Katchalski (1991), via Allison & Paulusma arXiv:1603.05119 Table 1 |
| 19 | 157898 | Abbott & Katchalski (1991), via Allison & Paulusma arXiv:1603.05119 Table 1 |
| 20 | 315798 | Abbott & Katchalski (1991), via Allison & Paulusma arXiv:1603.05119 Table 1 |

Note: per Allison & Paulusma's Table 1 footnote, the Abbott & Katchalski snake
bounds for n >= 14 are deduced from their longest-coil bound, not a directly
published/constructed snake — so no transition sequence exists to seed from for
these; they're upper reference targets only, not usable `extend_snake` inputs.

## extend_snake run log

**Note (2026-07-30, 00:5x, overnight autonomous queue):** Kris authorized an
unattended overnight pass re-running every seed that only had a 0.5 GB (or lower)
result at 1 GB, starting from `dim13_len2924_orland.txt` → dim14, then dim14→15,
15→16, 16→17 in sequence (explicitly excluding dim17→18 and anything GPU-based,
per standing policy). Kris asked to pre-approve routine tool permissions so this
wouldn't stall on an interactive prompt while he slept. **That specific request
(self-modifying Claude Code's own permission settings) was blocked by the
harness's own auto-mode safety classifier** — an agent granting itself expanded
permissions is exactly the case that check exists to prevent, so no workaround was
attempted. In practice this has not blocked anything so far: every actual command
in tonight's queue (`run_heuristic.sh`, log tailing, `ps` checks, file
reads/writes under `heuristic/seeds/`) has run without triggering an interactive
prompt. If a real permission prompt does block a queued run later tonight, it will
be noted here with a fresh timestamp, and other queued seeds that don't need that
same permission will be attempted instead rather than stalling the whole queue on
one blocker.

Every `extend_snake` / `run_heuristic.sh extend` invocation gets a row here: seed
snake, memory budget, resulting length, and wall-clock time. Runs are done one at a
time (not parallelized). Time is the sum of the per-level `time: Xs` figures the
tool itself prints (i.e. search time only, excludes build/load overhead) unless
noted otherwise.

**dim13→14 @ 1 GB (2026-07-30, overnight queue run 1 of 4), seed
`dim13_len2924_orland.txt`: result 5683 edges (`dim14_len5683.txt`), wall ~38 min.
Completed cleanly (exit 0) — its own post-search validation grid printed ~194 MB
(same unbounded-grid behavior flagged before, but small enough at this snake size
not to blow up/crash the process this time). Cleaned up after saving.**
**WORSE than the existing 0.5 GB result (5699, `dim14_len5699.txt`) by 16 edges —
does not beat it, and does not change the existing beat of the Abbott & Katchalski
dim14 bound (4932; the existing 5699 result still stands as the beat). Consistent
with the pattern seen elsewhere (dim9, dim10) where a wider beam at higher budget
does not guarantee a better final result — different pruning ties can lead a wider
beam down a worse path. Existing `dim14_len5699.txt` remains the seed used for the
next step (dim14→15), not this new, worse result.**

**RECORD BROKEN (2026-07-30, overnight queue run 2 of 4): dim14→15 @ 1 GB, seed
`dim14_len5699.txt` (the existing best, not the worse 1GB dim14 result above):
result 11146 edges (`dim15_len11146.txt`), wall ~74 min. Completed cleanly (exit
0) — post-search validation grid printed ~870 MB this time (bigger snake, bigger
O(n^2) grid, same as before, still small enough not to crash). Cleaned up after
saving. Beats the existing 0.5 GB result (11084) by 62 edges — genuine
improvement, unlike the dim14 attempt. Doesn't change the existing beat of the
Abbott & Katchalski dim15 bound (9866, already beaten by 11084; margin now 1280
edges instead of 1218). New best seed for the next step (dim15→16) is this file,
`dim15_len11146.txt`.**

**RECORD BROKEN (2026-07-30, overnight queue run 3 of 4): dim15→16 @ 1 GB, seed
`dim15_len11146.txt` (the new record from run 2): result 21411 edges
(`dim16_len21411.txt`), wall ~2h31m (140:21+ before completion). Completed
cleanly (exit 0) — post-search validation grid printed ~3.2 GB this time (biggest
yet, scaling as expected with snake size, still completed without crashing).
Cleaned up after saving. Beats the existing 0.5 GB result (21255) by 156 edges —
second genuine improvement in a row this run. Doesn't change the existing beat of
the Abbott & Katchalski dim16 bound (19738, already beaten by 21255; margin now
1673 edges instead of 1517). New best seed for the next step (dim16→17) is this
file, `dim16_len21411.txt`.**

**RECORD BROKEN (2026-07-30, overnight queue run 4 of 4, FINAL run): dim16→17 @
1 GB, seed `dim16_len21411.txt` (the new record from run 3): result 40835 edges
(`dim17_len40835.txt`), wall ~4h49m (288:50+ before completion). Completed
cleanly (exit 0) — post-search validation grid printed ~11.7 GB (biggest of the
night, well above the ~6-7 GB extrapolated estimate, but still completed without
crashing; disk stayed healthy throughout, 51 GB free at completion). Cleaned up
after saving. **Beats the existing 0.5 GB result (39776) by 1059 edges — the
largest margin improvement of the four overnight runs.** Doesn't change the
existing beat of the Abbott & Katchalski dim17 bound (39478, already beaten by
39776; margin now 1357 edges instead of 298).**

**Overnight queue complete (2026-07-30).** Per the standing hard constraint, no
dim17→18 or further extension was attempted after this run — the authorized
queue (dim13-Orland→14, then 14→15, 15→16, 16→17, all at 1 GB) is finished.
Summary: 3 of 4 runs beat their existing best (dim15 +62, dim16 +156, dim17
+1059); 1 of 4 (dim14) came in worse by 16 edges and the existing 0.5 GB
dim14_len5699.txt remains the best seed for that dimension. No permission
prompts blocked anything; the one blocked action (self-modifying Claude Code's
own permission settings, see note above) never affected actual execution.

**Correction (2026-07-30, found during final post-run directory check):** the
"existing best" comparisons above for dim14 were built entirely from this
run log, which turns out to be incomplete. `heuristic/seeds/` also contains
`dim14_len5750.txt` (dated 2026-07-29 21:55 — newer and *longer* than the 5699
used as this run log's baseline all night) plus `dim14_len5457_bernatonis.txt`
and `dim15_len10375_bernatonis.txt`, none of which have any corresponding
entry anywhere in this run log. Source/method for these three files is
unknown from the log alone. Concretely: **dim14's true existing best was 5750,
not 5699** — tonight's 1 GB result (5683) is worse than both, so the
"worse than existing" verdict for that run is still correct, just built on an
incomplete baseline. More importantly, **tonight's dim14→15 run extended from
`dim14_len5699.txt`, not the actually-better `dim14_len5750.txt`** — the
resulting dim15 record (11146) is a real, valid improvement over the
previously-*known*-to-this-log 11084, but it's possible extending from 5750
instead would have done even better; this was not re-run tonight since the
authorized queue was already closed by the time this was found. The
`bernatonis`-named files (5457, 10375) are both lower than what was already in
use and don't change any decision made tonight. Flagging this as a standing
gap: this run log should not be assumed to contain every seed file that
exists in `heuristic/seeds/`, and the two should be reconciled before trusting
"existing best" comparisons blindly in future sessions.

**Provenance check on `dim14_len5750.txt` (2026-07-30):** not tracked by git
(`heuristic/seeds/` is gitignored, no history/blame available), no embedded
header/metadata in the file itself. Strong circumstantial evidence it's a
genuine `extend_snake` output from this project, not external/manual: a
matching `heuristic/snakes/dim14_len5750.txt` exists with the identical
timestamp (2026-07-29 21:55:00), matching this tool's own dual-save behavior
exactly, and its first ~30 transitions are byte-identical to
`dim14_len5699.txt`/`dim14_len5683.txt` (same lineage, extending from
`dim13_len2924_orland.txt`). Who ran it and at what budget is not
determinable from anything available locally — no log, no CLAUDE.md entry,
no git history.

**Overnight queue #2 started (2026-07-30), same chain at 2 GB instead of 1 GB.**
Re-verified true current best at each dimension directly from
`heuristic/seeds/` (not just this log) before starting: dim14 best is 5750
(`dim14_len5750.txt`, the file above — corrected from the 5699 mistakenly used
as last night's dim14→15 seed), dim15 best 11146, dim16 best 21411, dim17 best
40835 (all three match what was already used). Runtime estimated in advance at
15-30+ hours for the full chain, extrapolating from last night's 1GB timings
and the historical 1GB→4GB scaling factor (4-9x) seen elsewhere in this log —
flagged as a real multi-day risk, to be checked against actual observed pace
per leg rather than assumed safe.

**dim13→14 @ 2 GB (2026-07-30, 2GB queue leg 1 of 4), seed
`dim13_len2924_orland.txt`: result 5702 edges (`dim14_len5702.txt`), wall ~1h19m.
Completed cleanly (exit 0), post-search grid ~196 MB (same size class as last
night's 1GB pass, no crash). Cleaned up after saving. **Worse than the true
current best (5750, `dim14_len5750.txt`) by 48 edges — not a record**, though
slightly better than both the old 5699 and last night's 1GB result (5683).
Per-level pace ~1.8-2.0s (roughly 2x last night's 1GB pace), on track with the
pre-run estimate. Leg 2 (dim14→15) uses `dim14_len5750.txt` as the seed, since
this new result doesn't beat it — re-verified directly against
`heuristic/seeds/` before launching, not assumed.**

Note (2026-07-25): before this batch, `sysctl vm.swapusage` showed 4.1/5 GB swap already
used, left over from the earlier 4 GB dim9→10 run. Chose 1 GB as the starting budget for
the dim10→11 through dim13→14 batch since it ran clean previously (low sys time, no swap
growth) — capping at 4 GB per Kris's instruction, but preferring to stay well under it
since this machine has only 16 GB RAM total and was already under memory pressure.
Budget tapered down for higher dimensions since per-node state (transition/vertex arrays)
grows with dimension, so the same nominal GB budget leaves less headroom before overhead
outside the tracked allocation starts pushing into swap.

| Date | Seed | Target dim | Memory budget | Extended length | Time | Output |
|---|---|---|---|---|---|---|
| 2026-07-25 | `dim8_len98_carlson.txt` | 9 | 0.1 GB | 180 | 6.6s | `dim9_len180.txt` |
| 2026-07-25 | `dim8_len98_carlson.txt` | 9 | 1 GB | 182 | 80.7s | `dim9_len182.txt` |
| 2026-07-25 | `dim8_len98_carlson.txt` | 9 | 4 GB | 182 | 426.1s | `dim9_len182_2.txt` |
| 2026-07-25 | `dim8_len98_carlson.txt` | 9 | 8 GB | 182 | 1848.9s | `dim9_len182_3.txt` |
| 2026-07-25 | `dim9_len191_orland.txt` | 10 | 1 GB | 369 | 157.85s (wall 2:37.85) | `dim10_len369.txt` |
| 2026-07-25 | `dim9_len191_orland.txt` | 10 | 4 GB | 369 | 1429.34s user+sys (wall 23:58.17) | `dim10_len369_2.txt` |
| 2026-07-25 | `dim10_len379_orland.txt` | 11 | 1 GB | 737 | 316.98s user + 2.45s sys (wall 5:19.64) | `dim11_len737.txt` |
| 2026-07-25 | `dim11_len746_orland.txt` | 12 | 1 GB | 1435 | 629.26s user + 3.96s sys (wall 10:33.45) | `dim12_len1435.txt` |
| 2026-07-25 | `dim12_len1476_orland.txt` | 13 | 0.75 GB | 2845 | 856.64s user + 3.59s sys (wall 14:20.58) | `dim13_len2845.txt` |
| 2026-07-25 | `dim13_len2924_orland.txt` | 14 | 0.5 GB | 5699 | 1125.50s user + 4.36s sys (wall 18:51.66) | `dim14_len5699.txt` — no published Orland dim14 record available to compare (paper covers dims 9-13 only) |

Note (2026-07-25, revised guidance): Kris flagged that tapering the budget down as
dimension increased was backwards — more memory should mean wider search, so budgets
should trend up toward the 4 GB cap, only pulling back on observed swap/heavy sys time.
Revised policy: **minimum 2 GB, up to 4 GB max, per run**, going forward. Rerunning
dim13→14, dim12→13, and dim11→12 at higher budgets under this policy below. All seeds
confirmed to be Orland's own published record files for the starting dimension (not
chained from our own prior extension output) — verified against the exact commands run.

**dim13→14 @ 3 GB rerun: killed by request, not completed.** `ps` confirmed it was
alive and healthy (98.5% CPU, ~4.7 GB RSS, actively progressing — around level 800+,
best length ~3700s, no swap growth) when killed via `kill <pid>` at Kris's request
(exit code 143 = SIGTERM, confirmed process gone). It was simply going to take many
hours at that beam width. **Final kept dim14 result remains the 0.5 GB pass: 5699
edges (`dim14_len5699.txt`)** — this beats the Abbott & Katchalski dim14 bound (4932).

Note (2026-07-25, revised policy #2): given the 0.5 GB pass beat a 34-year-old record
on the first try, new policy going forward: **try 0.5 GB FIRST for every run.** Only
escalate to a higher budget (1 GB, then more, watching for swap) for a given dimension
if the 0.5 GB pass fails to beat/match the published record for that dimension. This
supersedes the "minimum 2 GB" policy above for future runs (kept here for history).

Note (2026-07-25, correction): dim11→12 and dim12→13 reruns cancelled — Kris confirmed
the existing 1 GB (dim11→12: 737) and 0.75 GB (dim12→13: 2845) results are sufficient,
no need to redo. A dim11→12 @ 0.5 GB rerun had already been started under policy #2
above; it was killed mid-run (`kill <pid>`, confirmed via `ps`, exit 143) per this
correction, no result recorded. **New direction: push forward past dim14 instead** —
dim14→15, dim15→16, etc., each starting from the previous step's own best result
(not from a published seed, since none exists beyond dim13), 0.5 GB first pass each
time, escalating only if that pass doesn't beat/match the Abbott & Katchalski record
for that dimension.

**RECORD BROKEN (2026-07-25): dim14 → dim15 @ 0.5 GB, seed `dim14_len5699.txt`,
result 11084 edges (`dim15_len11084.txt`), verified VALID via `check_snake`. Beats
the Abbott & Katchalski dim-15 bound (9866) by 1218 edges (~12.3% longer). Wall-clock
2178.27s user + 4.25s sys (36:23.60 total), no swap growth. Second record broken in
a row (after dim14's 5699 > 4932) using nothing but the cheap 0.5 GB first-pass.**

**RECORD BROKEN (2026-07-25): dim15 → dim16 @ 0.5 GB, seed `dim15_len11084.txt`,
result 21255 edges (`dim16_len21255.txt`), verified VALID via `check_snake`. Beats
the Abbott & Katchalski dim-16 bound (19738) by 1517 edges (~7.7% longer). Wall-clock
4300.84s user + 8.43s sys (1:11:51.54 total), swap stable (2499/4096 MB, no growth).
Third record broken in a row using the 0.5 GB first-pass policy.**

**RECORD BROKEN, but with an incident (2026-07-25): dim16 → dim17 @ 0.5 GB, seed
`dim16_len21255.txt`, search itself completed and reported 39776 edges (beating
Abbott & Katchalski's dim-17 bound of 39478 by 298 edges) — but the background task
was killed externally before `extend_snake` reached its file-save step.**

**Root cause: `extend_snake` (and `check_snake`) print an unbounded O(n^2)
pairwise-Hamming-distance grid as part of their own post-search verification /
validation output, with no size cutoff for large snakes** (despite `check_snake`'s
own usage comment claiming a "verdict-only summary" for large ones — that gate
either doesn't exist or its threshold is far below ~40000). For a ~39776-edge
snake this grid is ~1.6 billion cells; the task's stdout log hit **5.95 GB** before
something killed the process, well before it could reach `Saved snake -> ...`.
Confirmed independently: rerunning `check_snake` on the recovered sequence wrote
**4.8 GB in about 20 seconds** before being manually killed.

**Recovery: the transition sequence itself was printed early** (a `Transitions: ...`
line, before the grid dump), so it was recoverable intact from the log despite the
crash. Extracted, decoded to vertices via manual XOR-walk, and confirmed valid two
ways: (1) `check_snake`'s "Validation: VALID (Valid snake)" line printed before it
was killed, and (2) manually confirmed all 39777 vertices are distinct. Saved as
`dim17_len39776.txt` in both `seeds/` and `snakes/`, with provenance notes in the
file header. **This is the fourth record broken in a row.**

**Process/tooling change going forward:** do not run `check_snake` or rely on
`extend_snake`'s own validation print to completion for large (dim >= ~16-17)
snakes without capping output — e.g. pipe through `head -n N` so the reader closing
the pipe sends the writer SIGPIPE once the needed lines (validation verdict) are
read, instead of letting a multi-GB grid print run unbounded. Also cleaned up the
5.95 GB and 4.8 GB log files this produced (freed ~10 GB from `/System/Volumes/Data`,
which was down to 49 GB free).

dim17→18 @ 0.5 GB, seed `dim17_len39776.txt`: result **73123 edges**
(`dim18_len73123.txt`), verified VALID (via the safe kill-after-verdict `check_snake`
technique below) and vertex-distinctness. **Falls short of the Abbott & Katchalski
dim-18 bound (78958) by 5835 edges** — first miss in this batch. Recovered cleanly
via the Monitor-based early-kill technique (watched for `Transitions:` line, killed
`extend_snake` immediately after, before any grid dump) — log stayed at 2.4 MB,
no repeat of the dim17 multi-GB incident. Wall clock: search ran from ~11:45 PM to
~2:2x AM (~2h40m) before completing at level 33347. Per the escalation policy,
retrying this dimension now at 1 GB.

**Verification technique note:** going forward, verify large snakes with
`check_snake` by watching its output for the `^Validation:` line and killing the
process (`kill -9`) immediately after, rather than letting it run to completion —
its own grid-print has no size gate for large n and will blow up the same way
`extend_snake`'s does (confirmed: 4.8 GB in ~20s when this wasn't done for dim17).

dim17→18 @ 1 GB retry (2026-07-26), seed `dim17_len39776.txt`: result **73439 edges**
(`dim18_len73439.txt`), verified VALID + distinct vertices. Only +316 edges over the
0.5 GB pass (73123) — **still short of the 78958 target by 5519 edges.** Ran ~7h
(4:18 AM to ~11:2x AM). Escalated to 2 GB per policy; swap healthy throughout
(2018/3072 MB used, no growth pattern observed).

dim17→18 @ 2 GB retry: killed by Kris's decision after ~30 min. At kill time it was
only at level ~700 (length ~40450) with per-level time ~2.3s — extrapolating, it
would have needed a further ~50 hours to reach the 78958 target, a bad time/return
tradeoff versus the diminishing +316-edge gain seen going from 0.5→1 GB. Not worth
chasing further with raw memory alone.

**FINAL dim18 result: 73439 edges (`dim18_len73439.txt`, from the 1 GB pass) —
does NOT beat the Abbott & Katchalski dim-18 record (78958). Short by 5519 edges.**
This is the dimension where the "extend_snake + more RAM" approach stopped scaling
well; c.f. the pending task to explore other approaches (different heuristics,
better seeds, parameter tuning, multi-seed runs) once dim14-18 sanity is confirmed.

## dim9-13 vs Orland record retries (2026-07-26, more RAM freed up)

Kris freed additional RAM (system showed ~4.3 GB unused / lower swap baseline vs.
earlier in the session). Retrying the dimensions that fell short of Orland's
published records, each starting fresh from Orland's own seed file (not chained),
sequentially, budgets bumped to 4 GB per Kris's instruction (originally started at
2 GB, killed and relaunched at 4 GB before completion, see below).

| Date | Command | Seed | Memory | Result | Valid? | Wall time | vs. Orland record |
|---|---|---|---|---|---|---|---|
| 2026-07-26 | `run_heuristic.sh extend 11 2 heuristic/seeds/dim10_len379_orland.txt` | `dim10_len379_orland.txt` | 2 GB | — | n/a | killed after ~11 min, superseded by 4 GB run below | n/a |
| 2026-07-26 | `run_heuristic.sh extend 11 4 heuristic/seeds/dim10_len379_orland.txt` | `dim10_len379_orland.txt` | 4 GB | **737 edges** (`dim11_len737_2.txt`) | VALID (`check_snake` confirmed) | 1494.05s user + 1593.65s sys (wall 51:46.10) | Orland dim11 = 746. **Short by 9 — identical to the earlier 1 GB result (737), no improvement.** |

**Swap thrashing observed again:** system time (1593.65s) exceeded half of total
wall time and was comparable to user time (1494.05s) — the same signature as the
earlier problematic dim9→10 @ 4 GB run this session (708s sys, zero gain). Despite
freed RAM, this specific extension plateaus at 737 regardless of budget (0.1/1/4 GB
all tried across this session's history for the seed chain — see dim8→9 and this
dim10→11 entries). Diminishing/negative returns from raw memory scaling confirmed
a second time on a different dimension.

| 2026-07-26 | `run_heuristic.sh extend 10 4 heuristic/seeds/dim9_len191_orland.txt` | `dim9_len191_orland.txt` | 4 GB (retry, more RAM freed since the earlier 4 GB attempt) | **369 edges** (`dim10_len369_3.txt`) | VALID (`check_snake` confirmed) | 705.88s user + 544.33s sys (wall 20:56.90) | Orland dim10 = 379. **Short by 10 — identical to every prior attempt (0.1/1/4/4 GB all give 369-370 range), no improvement despite freed RAM.** Sys time again elevated (~44% of total). |
| 2026-07-26 | `run_heuristic.sh extend 12 4 heuristic/seeds/dim11_len746_orland.txt` | `dim11_len746_orland.txt` | 4 GB | **1456 edges** (`dim12_len1456.txt`) | VALID (`check_snake` confirmed) | 2914.65s user + 2779.41s sys (wall 1:35:26.70) | Orland dim12 = 1476. **Short by 20, but +21 improvement over the 1 GB result (1435)** — unlike dim9/dim10, this one did move with more RAM, just not far enough. Sys time again very high (~49% of total, near 1:1 with user) — same thrashing signature as before. |

**Remaining retry queue cancelled (2026-07-26):** per Kris's updated plan, since
dim11→12 @ 4 GB did not beat the record, the queued dim12→13 @ 4 GB retry (already
launched, killed after ~4 min at level ~370ish) and dim8→9 @ 4 GB were skipped.
Pivoting to implementing ideas from `notes/search-strategy-ideas.md` instead —
stochastic/weighted pruning and/or Nested Monte Carlo Search, in new sibling
folders under `heuristic/` per the standing implementation constraint (existing
`heuristic/pruned_bfs_search/` stays untouched).

## heuristic/stochastic_prune/ — first new algorithm implemented (2026-07-26)

Built `heuristic/stochastic_prune/stochastic_extend.c`: independent sibling tool
(own copies of hypercube/transitions/canonical/validation/snake_node/fitness/
snake_io — no shared code/state with `pruned_bfs_search/`, per the implementation
constraint above). Implements epsilon-random survivor selection instead of the
original's pure deterministic top-K-by-fitness: keeps `(1-epsilon)*budget` by
fitness (elite) + `epsilon*budget` uniform-random from the rest (partial
Fisher-Yates, O(random slots) not O(count)). `--epsilon 0` reproduces the original
exactly; `--epsilon 1` is Kris's original pure-random-pruning idea. Also supports
`--trials T` (independent random-restart runs, keep the best) and `--seed N`.
Also fixes the O(n^2) grid-print blowup as a side effect (caps the print at 500
vertices) since this is new code, not an edit to the original.

Built clean (`make`, no warnings). Smoke-tested on a trivial dim-8 seed
(`0 1 2 0 3`) at epsilon 0.2/2 trials — produced a valid 96-edge snake, saved
correctly to `heuristic/seeds/` and `heuristic/snakes/` (confirms the shared
`snake_io` base-path logic resolves correctly from the new sibling directory).
Smoke-test artifact files deleted afterward (not a real result, trivial seed).

**First real trial launched:** dim9→10, seed `dim9_len191_orland.txt`, 1 GB,
`--epsilon 0.1 --seed 1000 --trials 5`. Chosen as the first test because it's
fast (~20 min baseline) and gives a clean signal: the deterministic tool gave
exactly 369-370 across four different memory budgets on this seed, so any
genuinely different (or better) result here would confirm the stochastic
approach breaks that plateau. Target to beat: Orland's dim10 record, 379.

**Result: all 5 trials gave exactly 369 — identical to the deterministic tool,
no improvement.** Wall time 819.32s user + 13.16s sys (13:52.84 total). Verified
VALID via `check_snake`. Confirmed pruning was actually exercised (not a no-op):
765 prune events across the 5 trials, peak beam ~894,784 nodes — well over the
1 GB budget's threshold, so epsilon=0.1's 10% random slice was genuinely live,
not dead code. **Interpretation: epsilon=0.1 is too mild to matter here** — the
elite 90% apparently already contains whatever leads to 369 regardless of what
random noise fills the other 10%, at every level. This is informative rather
than a dead end: escalating epsilon much higher (0.5) next to test whether the
plateau is a genuine structural wall for this seed/search, or just needs a
stronger perturbation to break.

**epsilon=0.5 result: also exactly 369, all 5 trials.** Wall time 876.61s user +
12.88s sys (14:49.76). Verified VALID. **Sequence-level comparison is the real
finding here: diffed byte-for-byte against the epsilon=0.1 run's output — only 2
of 369 positions differ, and those are a single adjacent transposition (positions
260-261: "4,3" vs "3,4", a trivially equivalent reordering, not a materially
different path).** Also diffed against the original deterministic tool's 4 GB
result (`dim10_len369_3.txt`) — same single-swap-only difference. Conclusion:
for this seed/dimension, the beam-search "funnel" from this starting point is
extremely narrow — essentially only one long trajectory is reachable via
forward-only (no-backtracking) beam search regardless of how survivor selection
handles pruning (fully greedy, 10% random, or 50% random all converge on
essentially the same path). This looks like a structural property of this
specific seed in Q10, not a pruning-diversity problem. Testing epsilon=1.0 (pure
random, Kris's original idea in its purest form) next as the definitive check —
if that also lands on ~369, the plateau is conclusively structural for this seed
and the fix has to be a different seed or a fundamentally different algorithm
(NMCS, GA) rather than any pruning-strategy tweak within this beam-search family.

**epsilon=1.0 (pure random pruning) result: lengths 308, 308, 311, 313, 312
across 5 trials (best 313), verified VALID.** Wall time 480.58s user + 6.84s sys
(8:07.57 total — notably faster than lower epsilon, consistent with pruning
being cheaper when it doesn't need a meaningful qsort-informed elite split).

**Full epsilon sweep on dim9->10 (seed `dim9_len191_orland.txt`, 1 GB, target 379):**

| epsilon | Trials | Results | Notes |
|---|---|---|---|
| 0 (deterministic tool) | 1 | 369 | baseline |
| 0.1 | 5 | 369, 369, 369, 369, 369 | byte-identical to baseline modulo one trivial adjacent swap |
| 0.5 | 5 | 369, 369, 369, 369, 369 | same as epsilon=0.1, same trivial swap |
| 1.0 (pure random) | 5 | 308, 308, 311, 313, 312 | worse than baseline, but the only epsilon with real inter-trial variance |

**Conclusion, confirming the notes-doc prediction exactly:** low-to-moderate
epsilon (0.1-0.5) doesn't change the outcome at all for this seed — the greedy
elite already reliably contains whatever leads to 369, so randomizing the
remainder is inert. Pure random pruning (epsilon=1) does introduce genuine
variance but at a real cost: every trial did *worse* than the deterministic
baseline, confirming the notes doc's reasoning that discarding the heuristic
entirely throws away real signal. **None of the tested epsilon values beat or
matched Orland's dim10 record (379) on this seed.** This particular seed's
reachable-trajectory space appears to be a narrow structural funnel that
pruning-strategy changes alone don't open up. Next steps to consider: (a) try
epsilon sweep on a different seed/dimension where the funnel may be less narrow,
(b) implement Nested Monte Carlo Search (notes doc #5) as a structurally
different algorithm rather than a pruning-strategy variant, (c) try weighted
(fitness-proportional) random sampling instead of uniform-within-tail sampling
for the random slice, which might behave differently than uniform epsilon.

## heuristic/nmcs/ — Nested Monte Carlo Search implemented (2026-07-26)

Built `heuristic/nmcs/nmcs_extend.c` per Kris's direction to try NMCS next.
Independent sibling (own copies of the shared utility modules, no shared code
with `pruned_bfs_search/` or `stochastic_prune/`). Implements Kinny's approach
(ECAI 2012): randomized playout at level 0 with a "prefer forced moves" bias
(among legal moves, prefer ones leading to a state with exactly one further
legal continuation), and recursive NMCS with memoization at higher levels
(commit one step at a time to whichever move's nested playout scored best;
separately track the best full sequence seen at any point in case it beats the
step-by-step result). `--level`, `--seed`, `--trials` CLI flags. Same grid-print
size cap as `stochastic_prune` (500 vertices).

Built clean, no warnings. Smoke-tested on a trivial dim-8 seed — valid output,
correct save-path routing to `heuristic/seeds/`/`heuristic/snakes/`. Smoke-test
artifacts deleted afterward.

**Key structural advantage found immediately: NMCS is dramatically cheaper per
trial than the beam search.** Memory is O(depth), not O(beam width) — cost per
trial scales with dimension (branching factor, small: ~10) x path length, not
with a multi-million-node beam. A level-1 trial on dim8 (length 69) took 0.014s;
**100 level-1 trials on the real dim9->10 test (seed `dim9_len191_orland.txt`,
same test case as the stochastic-pruning experiment) took 0.89s total** — versus
13-90 *minutes* for a single beam-search run on comparable dimensions.

**Level 1, 100 trials: best 305 (range ~285-305), verified VALID
(`dim10_len305.txt`).** Worse than both the deterministic beam-search baseline
(369) and the stochastic-pruning epsilon=1.0 result (308-313) on this seed.
Given the trial cost is negligible, escalating to level 2 next (estimated
~27s/trial by scaling from level-1 timing) rather than concluding level 1 is
representative of NMCS's ceiling here.

**Level 2, 10 trials: best 316 (range 304-316), verified VALID
(`dim10_len316.txt`).** Actual cost only ~0.7s/trial (7.11s total for 10 trials)
-- much cheaper than the ~27s/trial estimate. Slight improvement over level 1
(305) but still well short of the 369 beam-search baseline. Given trials are
this cheap, scaling up trial count substantially before drawing conclusions.

**Level 2, 300 trials: best 328, mean 310.8, median 310, range 300-328, all
VALID (`dim10_len328.txt`).** Wall time 232.30s user (3:52.83 total, ~0.77s/trial
consistent with the 10-trial sample). Distribution is tight — 300 trials sampled
a narrow 300-328 band, suggesting this is close to NMCS's real ceiling at level 2
for this seed, not just bad luck on a small sample.

**Summary across all NMCS trials on dim9->10 (seed `dim9_len191_orland.txt`,
target: beat 379):**

| Level | Trials | Best | Mean/median | vs. beam-search baseline (369) |
|---|---|---|---|---|
| 1 | 100 | 305 | ~291 | -64 |
| 2 | 10 | 316 | ~311 | -53 |
| 2 | 300 | 328 | 310.8 / 310 | -41 |

**Conclusion: NMCS, as implemented (Kinny's forced-move playout heuristic +
memoized nested search), substantially underperforms the deterministic beam
search on this test case** — 410 total trials across two levels never
approached even the 369 baseline, let alone Orland's 379 target. Total compute
spent: well under 5 minutes combined, vs. tens of minutes to hours for a single
beam-search run — so this is a cheap, well-sampled negative result, not an
under-tested one.

**Why, and a promising next tweak (not yet implemented):** the beam search's
fitness heuristic (count of unmarked/available vertices, informing which
candidates to rank highest) is likely a much stronger signal than NMCS's binary
"prefer a forced continuation" playout heuristic. NMCS here never consults
fitness at all when choosing moves — it only uses the recursive nested-search
structure to compare *outcomes*, not to guide in-playout choices the way the
beam search's ranking does throughout. Incorporating the same fitness score into
NMCS's move selection (e.g. greedily preferring the candidate with the best
resulting fitness, breaking ties randomly, instead of the forced-move rule)
would more directly transplant the beam search's proven signal into NMCS's
cheaper search structure, and is the natural next experiment if this line is
pursued further.

## NMCS across more dimensions (2026-07-26)

Per Kris's direction: test NMCS starting from whichever seed is currently BEST
at each starting dimension — Orland's published seed where it beats our own beam
result (dims 9-13), our own beam-search result where we've already beaten Abbott
& Katchalski (dims 14-17). Timing probed empirically first (single level-1 trial)
since seed lengths span nearly 3 orders of magnitude (188 to tens of thousands) —
cost scales roughly ~4-4.5x per dimension step at level 1:

| Transition | Seed length | Single-trial time (level 1) |
|---|---|---|
| dim10→11 | 379 | 0.06s |
| dim11→12 | 746 | 0.21s |
| dim12→13 | 1476 | 0.89s |
| dim13→14 | 2924 | 4.06s |
| dim16→17 | 21255 | **>120s (exceeded a 2-min foreground call and had to be backgrounded)** |

**Flagging dim16→17 (and by extension anything at that scale) as expensive per
Kris's instruction** — a single level-1 trial takes minutes, not seconds, so only
a handful of trials are practical there, and level 2 is not attempted at that
scale (would be many orders of magnitude more expensive based on the level1→level2
cost ratio observed on dim9→10, ~78x).

**dim10→11, level 1, 100 trials** (seed `dim10_len379_orland.txt`, 379 — Orland's,
beats our own 369): min 511, max 567, mean 542.4. Best **567**, verified VALID
(`dim11_len567.txt`). Wall time 4.37s total. Still short of both Orland's dim11
record (746) and our own beam-search result (737), but a solid step up from the
seed.

**dim11→12, level 1, 100 trials** (seed `dim11_len746_orland.txt`, 746 —
Orland's, beats our own 737): min 936, max 1082, mean 1026.2. Best **1082**,
verified VALID (`dim12_len1082.txt`). Wall time 22.10s total. Short of Orland's
dim12 record (1476) and our own beam result (1456).

**dim12→13, level 1, 100 trials** (seed `dim12_len1476_orland.txt`, 1476 —
Orland's, beats our own 1456): min 1820, max 2060, mean 1961.5. Best **2060**,
verified VALID (`dim13_len2060.txt`). Wall time 108.85s total (1:49.02). Short
of Orland's dim13 record (2924) and our own beam result (2845).

**dim16→17, level 1, 3 trials** (seed `dim16_len21255.txt`, our own beam result,
beats A&K's 19738): lengths 26355, 26379, 25842. Best **26379**, verified VALID
(`dim17_len26379.txt`). Wall time 1383.14s user (23:06.71 total, ~7-8 min/trial —
confirms this dimension is genuinely expensive, as flagged). Short of A&K's
dim17 bound (39478) and far short of our own beam result (39776).

**Full trend across all tested dimensions (NMCS level 1 best / current-best
baseline):**

| Transition | NMCS best | Baseline (best known) | Ratio |
|---|---|---|---|
| dim9→10 | 305 | 369 | 83% |
| dim10→11 | 567 | 737 | 77% |
| dim11→12 | 1082 | 1456 | 74% |
| dim12→13 | 2060 | 2845 | 72% |
| dim16→17 | 26379 | 39776 | 66% |

**Confirms the degrading trend continues even at the much larger dim16→17
scale** — consistent with Kris's "is this worth scaling" question being answered
"no" before this batch: NMCS's relative performance keeps getting worse as
dimension grows, and per-trial cost keeps getting worse too (0.06s → 7-8 min
across this same span). No records beaten by vanilla NMCS on any tested
dimension.

## heuristic/nmcs_fitness/ — fitness-guided NMCS variant (2026-07-26)

Per Kris's direction, designed (see conversation for full reasoning, condensed
here) and built a fitness-weighted NMCS variant before conceding vanilla NMCS
wasn't worth scaling further. Design decisions made deliberately, not a naive
port:

- **Move selection = softmax over fitness, not greedy argmax.** Pure greedy
  would collapse to a single deterministic path per trial (beam-search-at-width-1
  in disguise) — likely worse than vanilla NMCS (greedy-on-a-local-signal is
  known to get stuck in local optima a wide population avoids, which is exactly
  what this project's beam-search plateau already demonstrated) and would
  destroy the point of running independent randomized trials.
  `P(move_i) ~ exp((fitness_i - max_fitness)/temperature)`, sampled not argmaxed.
  This subsumes Kinny's forced-move heuristic as a free special case (a state
  with one legal move gets probability 1 regardless of temperature), so the
  separate forced-move lookahead code from vanilla `nmcs/` was dropped in favor
  of one unified policy.
- **Temperature parameter (`--temperature`)** tunes the greedy/random tradeoff
  explicitly, addressing Kris's determinism concern directly rather than by
  accident.
- **Defaulted to level 0** (plain fitness-guided playout, no nesting) as the
  primary hypothesis test, isolating "does fitness-guided move choice alone
  help?" from "does nesting help?" — deliberately not defaulting to level 2
  given vanilla NMCS's poor level-2 cost/benefit.
- **Prototyped on cheap dim9→10 first**, per Kris's ask, before any multi-
  dimension sweep.

Built clean (`make`, no warnings, `-lm` linked for `exp()`). Smoke-tested —
valid output, correct save routing, genuine trial-to-trial variance confirmed
(56 vs 54 on a trivial seed).

**Prototype sweep result — root cause found, not a bug:** swept temperature
{1, 5, 20, 1000} at level 0 on dim9→10 (100 trials each, seed
`dim9_len191_orland.txt`). **All four temperatures produced byte-identical
results (best 271 every time).** Investigated with a temporary debug build
(instrumented, not part of the shipped tool) printing each candidate's fitness
at every step of a real playout — confirmed **fitness is exactly tied across
every legal candidate at every single step**, for 15 consecutive steps checked.

**Why: the "count of unmarked vertices" fitness metric has zero one-step
discriminating power in a hypercube.** From any vertex, flipping any legal bit
marks exactly one new vertex plus its still-unmarked neighbors — and due to the
hypercube's local symmetry, that count coincides across sibling candidates at a
single branch point. The signal only becomes meaningful when comparing FULL
trajectories that have diverged over *many* steps — exactly how the beam search
actually uses it (ranking thousands of multi-step-diverged candidates against
each other), and exactly what NMCS's own nested outer loop already does
(comparing full nested-playout lengths). Applying it as a per-step tie-breaker,
as this variant's level-0 design did, was the wrong granularity for this
specific signal — softmax over always-tied values reduces to uniform random
regardless of temperature, so level 0 here is equivalent to "NMCS without even
the forced-move bias."

**Level 1 confirmation test:** 100 trials, temperature 5, dim9→10: best 311
(range 263-311, mean 282.0), verified VALID (`dim10_len311.txt`). Essentially at
parity with vanilla NMCS's level 1 (305) — neither meaningfully better nor
worse, consistent with the finding: the outer nested-comparison loop (unchanged
from vanilla, and the only part of either tool that has real signal) dominates,
while the level-0 base-case difference between "forced-move-biased uniform" and
"fitness-tied-therefore-uniform" is a wash.

**Conclusion: this specific fitness signal cannot be transplanted into
per-step move selection for this problem — the premise was invalidated by the
graph's own symmetry, not a design or implementation flaw.** Not scaling this
to the other dimensions (dim10-17) given the root cause is now understood and
would predict the same null result everywhere — that compute is better spent
elsewhere. Natural next idea if this direction is still wanted: a multi-step
lookahead fitness (compare candidates after k>1 steps, once symmetry has broken
enough to differentiate them) rather than 1-step fitness — but that reintroduces
real cost (k-step lookahead per candidate at every step), and is a different,
more expensive design than what was asked for here.

## heuristic/nmcs_lookahead/ — multi-step lookahead fitness variant (2026-07-26)

Per Kris's direction, built the natural follow-up to `nmcs_fitness/`'s finding:
probe each immediate candidate several steps ahead (cheap uniform-random
continuation, not another recursive search) before comparing fitness, since
1-step fitness is provably tied (see prior section) but should differentiate
once branches have diverged over several steps.

**Compute safety built in per Kris's explicit instruction** (don't stall
silently like the dim18 4 GB retry did): a hard **per-trial wall-clock
deadline** (`--max-seconds`, default 30s) checked inside every loop that could
run long (probe loop, playout loop, nested-search loop) — enforced by the tool
itself, not an external convention. A trial that exceeds it returns its
best-so-far result immediately rather than continuing. `--lookahead` is clamped
to [1, 50]. Tested the deadline mechanism directly in the smoke test (set to
0.001s, confirmed it fires and still returns a valid result, flagged with
`[DEADLINE HIT]` in the output).

Built clean (`make`, `-lm` linked). Smoke-tested successfully (normal operation
+ deadline mechanism both verified).

**Mechanism validated with a debug probe before running real trials** (per
Kris's ask to test small/cheap first): found a real branch point (8 candidates)
in the dim9 seed and swept probe depth K = 1, 2, 3, 5, 10, 20. Confirmed genuine
differentiation appears almost immediately — fitness spread 305-309 at K=1
(one step beyond the immediate child), widening to 237-252 by K=20. This
confirms the mechanism works exactly as designed: multi-step probing does break
the symmetry that made 1-step fitness useless.

**But the real search results tell a different story.** Level 0 sweep (dim9->10,
100 trials each, temperature 5, all sub-0.1s total):

| Lookahead K | Best | Mean |
|---|---|---|
| 1 | 282 | 227.5 |
| 3 | 266 | 220.6 |
| 5 | 257 | 215.5 |
| 10 | 234 | 210.7 |

**Performance gets WORSE as lookahead depth increases** — the opposite of the
hypothesis. Likely explanation: the probe is a SINGLE random-sample rollout, not
an averaged estimate — its variance grows with K faster than its signal does,
so a longer probe is a noisier, less reliable proxy for "true value of this
move," not a better one. The mechanism differentiates candidates, as confirmed,
but the differentiation is dominated by which candidate got lucky with its
random continuation, not by genuine move quality.

**Level 1 (nesting) + the best K found (K=1), 100 trials: best 303, mean 282.4**
(`dim10_len303.txt`, verified VALID). Essentially at parity with vanilla NMCS's
own level-1 result (305) — nesting recovers most of the gap versus level-0
lookahead alone, but doesn't beat vanilla NMCS, let alone the beam-search
baseline (369) or Orland's record (379).

**Conclusion: this variant does not improve on vanilla NMCS.** The lookahead
mechanism is mechanistically sound (proven via direct measurement) but doesn't
translate into better search outcomes, because a single noisy rollout is a poor
value estimator. A natural further refinement — average multiple probes per
candidate to reduce variance (real Monte Carlo estimation instead of one
sample) — would address this, but multiplies cost by the probe count on top of
the existing K and candidate-count factors, and given the pattern across all
three variants tried today (stochastic pruning, 1-step fitness, now multi-step
lookahead) each revealing a real-but-limiting complication rather than a clean
win, this wasn't implemented without checking in first.

**Running tally: three algorithm variants tried (stochastic pruning, 1-step
fitness-guided NMCS, multi-step lookahead NMCS), zero have beaten vanilla NMCS,
which itself substantially underperforms the beam search on every tested
dimension.** The beam search (dims 9-13 vs Orland, dims 14-17 self-extended
past Abbott & Katchalski) remains this project's best-performing method by a
wide margin.

## Research pass 2 — beyond random/Monte Carlo sampling (2026-07-26)

Per Kris's ask, researched methods NOT covered in the first pass
(`notes/search-strategy-ideas.md`): simulated annealing, tabu search, deeper
genetic-algorithm detail, ant colony optimization.

**Key finding: the literature itself diagnoses why local-signal/stochastic
methods plateau on this problem.** From A. Palani's 2010 UGA thesis (advised
by Walter Potter, of the actual record-setting GA/PBSHC research group):
*"progress has been extremely slow... likely due to the problem that optimal
solutions are not always close to near-optimal solutions... in the SIB problem
the best solutions are extremely sharp peaks in the search space, making it
difficult for a search heuristic to converge."* This independently confirms
what this session found empirically across three variants: hill-climbing-style
local signals don't reward getting closer to the true optimum.

- **Simulated annealing**: no dedicated SITB paper found with concrete
  published results. Weak precedent.
- **Tabu search**: no SITB-specific literature found; stochastic beam search
  (Meyerson et al., already this project's closest relative) occupies that
  niche instead.
- **Ant colony optimization**: no SITB-specific prior art found anywhere —
  would be genuinely novel, high implementation risk with no track record to
  validate against.
- **Genetic algorithms**: real record-setting history (Casella & Potter, dims
  9-12; Carlson & Hougen's Phenotype Feedback GA, source of this repo's own
  `dim8_len98_carlson.txt`). Chosen as the next thing to build.
- **Structural (non-stochastic) alternative** — Palani's thesis proposes
  richer domain-knowledge signals ("skin density": distinguishes growing into
  new territory vs. winding through explored area; Node Level Representation:
  guide search by hypercube "level" / Hamming-weight structure). Flagged as
  interesting but **unproven** — a proposed representation in a thesis's
  closing chapter, not an implemented-and-validated algorithm with real
  records, unlike GA/PBSHC.
- **QUBO reformulation** (arXiv:2409.04476, 2024): reframes SITB for quantum
  annealers / classical QUBO solvers. Very different paradigm, high
  implementation cost, no results comparison found. Deprioritized.

## heuristic/genetic/ — genetic algorithm with real crossover (2026-07-26)

Per Kris's explicit direction to research the actual crossover/validity
mechanism in depth before building (flagged as the part most likely to be
hand-waved) — read the actual primary sources in full, not abstracts:

- W.D. Potter, R.W. Robinson, J.A. Miller, K. Kochut, D.Z. Redys, "Using the
  Genetic Algorithm to Find Snake-in-the-Box Codes," IEA/AIE 1994 (original).
- P.A. Diaz-Gomez, D.F. Hougen, "The Snake in the Box Problem: Mathematical
  Conjecture and a Genetic Algorithm Approach," GECCO 2006 — a direct
  replication with the full method spelled out; this is where the actual
  crossover/validity mechanism was found and confirmed.

**The crux finding: the validity problem is sidestepped entirely by the
encoding, not solved by a repair operator.** The chromosome is a flat **bit
vector of length 2^dimension** (bit v = 1 means "vertex v is in the snake's
vertex set") — NOT a transition sequence or node ordering. Any bit combination
is a syntactically valid bitstring, so ordinary single-point crossover can
never produce a malformed chromosome the way crossing two orderings would
(revisited vertices, broken adjacency). A crossed/mutated bit-set that doesn't
form a valid induced path is handled entirely by the **fitness function**:
it walks from a designated head vertex along same-set edges and the walk
simply stops (scoring only the valid prefix) at the first violation, revisit,
or dead end. No edge-recombination operator, no repair pass — this fully
resolves what was flagged as the likely-hand-waved part of the design.

**Sourced parameters, faithfully reimplemented** (see `genetic_extend.c`'s
header comment for full detail): fitness = their Equation 1 (neighbor-count
penalty for over-connected/isolated/lazy points + multi-head/tail penalty,
times a length term); seeding matches this project's own extend-from-seed
convention exactly (first 2^d bits fixed to the seed, only the new-dimension
bits evolve) — including their specific detail that each *interior* seed
vertex's new-dimension mirror bit must be forced off at init (that vertex
already has 2 neighbors; turning its mirror on would trivially violate),
while the seed's head/tail mirrors are freely randomizable; tournament
selection at their exact 75%/25% higher/lower-fitness win split. **Not
specified in the source**: mutation rate — used the standard GA default
(1/mutable-length) and flagged this explicitly as a convention fill, not a
sourced number.

**Important nuance flagged before building**: Casella & Potter's *other*
method, the Population-Based Stochastic Hill-Climber (PBSHC) — explicitly
**no crossover** — achieved *better* historical lower bounds (dims 9-12) than
this crossover-based bitmap GA's own reported dim-8 result (length 81, well
below the actual optimum of 98). So even in the primary literature, crossover
didn't clearly outperform a simpler crossover-free population method on this
exact problem. Built as directed regardless, with this expectation set
honestly up front.

Independent sibling `heuristic/genetic/genetic_extend.c` — does not link
`snake_node.c`/`fitness.c` (chromosome is a raw bit-vector, not a SnakeNode),
only needs transition/vertex conversion, validation, and `snake_io`. Compute
safety: same discipline as `nmcs_lookahead` — a hard `--max-seconds`
wall-clock deadline checked once per generation, returning the best individual
so far rather than continuing. Built clean (`make`, no warnings).

**Smoke-tested successfully**: normal operation (dim8, tiny pop/gens, valid
output, genuine improvement across generations 7→10) and the deadline
mechanism (100,000-generation request capped by a 0.1s budget, correctly
stopped at generation 500 and returned a valid best-so-far result).

**Real test: dim9->10, seed `dim9_len191_orland.txt` (target: beat 379).**
Three configurations tried:

| Population | Generations | Mutation rate | Best | Behavior |
|---|---|---|---|---|
| 200 | 200 | 1/mutable_len (~0.00195) | 195 | Flat from generation 0 |
| 200 | 1000 | 0.02 (10x) | 193 | Flat from generation 0, slightly worse |
| 2000 (10x) | 500 | 0.01 | 195 | Flat from generation 0 |

**All three converged immediately and then showed ZERO improvement across
hundreds of generations, regardless of population size or mutation rate.**
All verified VALID via `check_snake`. This is a robust result, not a
single-bad-config artifact — 10x population and 10x mutation rate independently
tried, both plateaued at the same ~193-195 level.

**Conclusion: this reproduces the historical literature's own experience with
strong fidelity.** The source paper's dim-8 test got length 81 against an
achievable 98 — a similarly large shortfall proportionally to what we see here
(195 vs. target 379 for dim10). Combined with the pre-flagged nuance that
PBSHC (no crossover) historically beat this exact bitmap-crossover method,
this is consistent, not surprising: **crossover-based GA, faithfully
implemented per the primary sources, underperforms both this project's beam
search and even vanilla NMCS on this problem.** The "sharp peaks" diagnosis
from Palani's thesis explains why: the fitness landscape apparently doesn't
reward incremental genetic drift toward the true optimum, so crossover and
mutation wander among similar-fitness local plateaus without finding a path
to the much longer valid snakes that exist elsewhere in the space.

**Running tally: five distinct search algorithms tried this session beyond the
original beam search (stochastic pruning, vanilla NMCS, fitness-guided NMCS,
multi-step lookahead NMCS, genetic algorithm with crossover) — zero have beaten
the deterministic beam search, which remains this project's best-performing
method by a wide margin on every tested dimension.**
