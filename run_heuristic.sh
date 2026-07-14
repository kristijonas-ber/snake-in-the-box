#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$REPO_ROOT/heuristic/pruned_bfs_search"

usage() {
    cat <<'EOF'
Usage: ./run_heuristic.sh <mode> [args...]

Builds and runs an algorithm from heuristic/pruned_bfs_search.
All args after <mode> are passed to the binary unchanged.

Modes:
  serial    ./snake_in_box    <dimension> [memory_gb]
  parallel  ./parallel_search <dimension> [memory_gb] [workers]
  priming   ./priming         <dimension> [memory_gb] [seed_file]
  extend    ./extend_snake    <dimension> [memory_gb] [--both-ends] [seed ...]

Defaults are the binaries' own: dimension 7, memory 18.0 GB, 10 workers.

Examples:
  ./run_heuristic.sh serial 7 18.0
  ./run_heuristic.sh parallel 7 18.0 10
  ./run_heuristic.sh priming 8 18.0 extend_input.txt
  ./run_heuristic.sh extend 8 18.0 --both-ends seed1.txt seed2.txt
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
    serial)   target="snake_in_box"    ;;
    parallel) target="parallel_search" ;;
    priming)  target="priming"         ;;
    extend)   target="extend_snake"    ;;
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

cd "$SRC_DIR"

if [ "$mode" = "parallel" ] && [ "$(uname -s)" = "Darwin" ]; then
    export OMPFLAGS="${OMPFLAGS:--Xpreprocessor -fopenmp -lomp}"
fi

echo "+ make $target" >&2
make "$target"

echo "+ ./$target $*" >&2
exec "./$target" "$@"
