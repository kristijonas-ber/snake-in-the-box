#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$REPO_ROOT/exhaustive/dfs_search"

usage() {
    cat <<'EOF'
Usage: ./run_exhaustive.sh [options] [-- extra_mpirun_args]

Builds and runs the exhaustive DFS search from exhaustive/dfs_search.
Search knobs are compile-time defines, so the binary is rebuilt for each run.

Search knobs (unset knobs keep their config.hpp defaults):
  -n, --dim N            hypercube dimension            (default 6)
  -p, --prefix-length L  prefix depth / task granularity (default 11)
      --slice-count C    split search into C independent runs
      --slice-id I       run slice I of SLICE_COUNT
      --root-offset O    window mode: first root index
      --root-count C     window mode: number of roots
      --knuth-probes K   Knuth probe count
      --probe-only       probe only, skip the full search
  -D KEY=VALUE           any other config.hpp knob (repeatable)

Run options:
      --procs P          MPI rank count                 (default 5)
      --replay           use ./dfs_search_replay (dispatcher-free)
      --oversubscribe    pass --oversubscribe to mpirun
      --no-clean         skip the forced 'make clean'
      --decode FILE      decode a .bin result file and exit (no MPI)
  -h, --help             show this help

dfs_search needs at least 2 ranks: rank 0 dispatches, the rest search.
'make clean' is forced by default because the Makefile's object rule does not
depend on DEFS, so a changed -DN would otherwise silently relink stale objects.

Examples:
  ./run_exhaustive.sh --procs 5 --oversubscribe
  ./run_exhaustive.sh --dim 8 --prefix-length 18 --procs 16
  ./run_exhaustive.sh --dim 8 --slice-count 64 --slice-id 0 --procs 32 --replay
  ./run_exhaustive.sh -D KNUTH_PROBES=1000 -D PROBE_ONLY=1
  ./run_exhaustive.sh --decode ../job_outputs/snakes_dfs_search/foo.bin

Output: exhaustive/job_outputs/
EOF
}

defs=()
procs=5
binary="dfs_search"
oversubscribe=0
do_clean=1
decode_file=""
mpirun_args=()

while [ $# -gt 0 ]; do
    case "$1" in
        -n|--dim)           defs+=("-DN=$2");             shift 2 ;;
        -p|--prefix-length) defs+=("-DPREFIX_LENGTH=$2"); shift 2 ;;
        --slice-count)      defs+=("-DSLICE_COUNT=$2");   shift 2 ;;
        --slice-id)         defs+=("-DSLICE_ID=$2");      shift 2 ;;
        --root-offset)      defs+=("-DROOT_OFFSET=$2");   shift 2 ;;
        --root-count)       defs+=("-DROOT_COUNT=$2");    shift 2 ;;
        --knuth-probes)     defs+=("-DKNUTH_PROBES=$2");  shift 2 ;;
        --probe-only)       defs+=("-DPROBE_ONLY=1");     shift   ;;
        -D)                 defs+=("-D$2");               shift 2 ;;
        --procs)            procs="$2";                   shift 2 ;;
        --replay)           binary="dfs_search_replay";   shift   ;;
        --oversubscribe)    oversubscribe=1;              shift   ;;
        --no-clean)         do_clean=0;                   shift   ;;
        --decode)           decode_file="$2";             shift 2 ;;
        -h|--help)          usage; exit 0                         ;;
        --)                 shift; mpirun_args=("$@"); break      ;;
        *)
            echo "run_exhaustive.sh: unknown option '$1'" >&2
            echo >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [ -n "$decode_file" ] && [ ! -f "$decode_file" ]; then
    echo "run_exhaustive.sh: no such file: $decode_file" >&2
    exit 2
fi

if [ -n "$decode_file" ]; then
    decode_file="$(cd "$(dirname "$decode_file")" && pwd)/$(basename "$decode_file")"
fi

cd "$SRC_DIR"

if [ "$do_clean" -eq 1 ]; then
    echo "+ make clean" >&2
    make clean
fi

echo "+ make DEFS=\"${defs[*]-}\"" >&2
make DEFS="${defs[*]-}"

if [ -n "$decode_file" ]; then
    echo "+ ./dfs_search --decode $decode_file" >&2
    exec ./dfs_search --decode "$decode_file"
fi

if [ "$binary" = "dfs_search" ] && [ "$procs" -lt 2 ]; then
    echo "run_exhaustive.sh: --procs must be at least 2 for dfs_search" >&2
    echo "  (rank 0 dispatches prefixes, so at least 1 more rank must search)" >&2
    echo "  use --replay for a dispatcher-free build that can run on 1 rank" >&2
    exit 2
fi

cmd=(mpirun)
if [ "$oversubscribe" -eq 1 ]; then
    cmd+=(--oversubscribe)
fi
cmd+=(-n "$procs")
if [ "${#mpirun_args[@]}" -gt 0 ]; then
    cmd+=("${mpirun_args[@]}")
fi
cmd+=("./$binary")

echo "+ ${cmd[*]}" >&2
exec "${cmd[@]}"
