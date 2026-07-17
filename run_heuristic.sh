#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TRACK_ROOT="$REPO_ROOT/heuristic"
SRC_DIR="$TRACK_ROOT/pruned_bfs_search"
ORIG_PWD="$PWD"

usage() {
    cat <<'EOF'
Usage: ./run_heuristic.sh <mode> [args...]

Builds and runs an algorithm from heuristic/pruned_bfs_search.
All args after <mode> are passed to the binary unchanged.

Modes:
  serial          ./snake_in_box    <dimension> [memory_gb]
  parallel        ./parallel_search <dimension> [memory_gb] [workers]
  priming         ./priming         <dimension> [memory_gb] [seed_file]
  extend          ./extend_snake    <dimension> [memory_gb] [--both-ends] [seed ...]
  parallel-extend ./parallel_extend <dimension> [memory_gb] [workers] [--both-ends] [seed ...]

Defaults are the binaries' own: dimension 7, memory 18.0 GB, 10 workers.

Examples:
  ./run_heuristic.sh serial 7 18.0
  ./run_heuristic.sh parallel 7 18.0 10
  ./run_heuristic.sh priming 8 18.0 extend_input.txt
  ./run_heuristic.sh extend 8 18.0 --both-ends seed1.txt seed2.txt
  ./run_heuristic.sh parallel-extend 14 18.0 16 --both-ends seeds/dim13_len2854_ace.txt
  ./run_heuristic.sh extend --help

Output: snakes -> heuristic/snakes/, transition sequences -> heuristic/seeds/
EOF
}

if [ $# -lt 1 ]; then
    usage
    exit 2
fi

mode="$1"
shift

case "$mode" in
    serial)          target="snake_in_box"    ;;
    parallel)        target="parallel_search" ;;
    priming)         target="priming"         ;;
    extend)          target="extend_snake"    ;;
    parallel-extend) target="parallel_extend" ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        echo "run_heuristic.sh: unknown mode '$mode'" >&2
        echo >&2
        usage >&2
        exit 2
        ;;
esac

args=()
for a in "$@"; do
    if [ -e "$ORIG_PWD/$a" ] && [ "${a#-}" = "$a" ]; then
        args+=("$(cd "$(dirname "$ORIG_PWD/$a")" && pwd)/$(basename "$a")")
    else
        args+=("$a")
    fi
done

make_args=("$target")

if { [ "$mode" = "parallel" ] || [ "$mode" = "parallel-extend" ]; } && [ "$(uname -s)" = "Darwin" ]; then
    if [ -n "${OMPFLAGS:-}" ]; then
        make_args+=("OMPFLAGS=$OMPFLAGS")
    else
        omp_prefix="$(brew --prefix libomp 2>/dev/null || true)"
        if [ -z "$omp_prefix" ] || [ ! -d "$omp_prefix" ]; then
            echo "run_heuristic.sh: libomp not found; parallel mode needs OpenMP." >&2
            echo "  install it with:  brew install libomp" >&2
            echo "  or set OMPFLAGS yourself to override." >&2
            exit 1
        fi
        make_args+=("OMPFLAGS=-Xpreprocessor -fopenmp -I$omp_prefix/include -L$omp_prefix/lib -lomp")
    fi
fi

echo "+ make -C ${SRC_DIR#"$REPO_ROOT"/} ${make_args[*]}" >&2
make -C "$SRC_DIR" "${make_args[@]}" >&2

cd "$TRACK_ROOT"

echo "+ (cd heuristic && pruned_bfs_search/$target ${args[*]-})" >&2
exec "pruned_bfs_search/$target" ${args[@]+"${args[@]}"}
