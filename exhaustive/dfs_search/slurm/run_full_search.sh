#!/usr/bin/env bash
# Rebuilds and runs dfs_search for every dimension N=1..6, writing results
# into exhaustive/job_outputs/. The binary writes "job_outputs/..." relative to
# its CWD, so this script runs it from exhaustive/, not from dfs_search/.
#
# PREFIX_LENGTH is picked per N so the deterministic prefix walk actually
# reaches that depth: it must stay below N's max snake length in vertices
# (1:2, 2:3, 3:5, 4:8, 5:14, 6:27), or the run silently finds nothing.
#
# Usage: ./run_full_search.sh
set -euo pipefail

cd "$(dirname "$0")/../.."   # this script lives in dfs_search/slurm/ -> exhaustive/
SEARCH_DIR="dfs_search"
PROCS=5                # 1 dispatcher + 4 workers

pl_for() {
    case "$1" in
        1) echo 1 ;;
        2) echo 1 ;;
        3) echo 2 ;;
        4) echo 3 ;;
        5) echo 6 ;;
        6) echo 11 ;;
    esac
}

for N in 1 2 3 4 5 6; do
    PL=$(pl_for "$N")
    echo "=== N=$N (PREFIX_LENGTH=$PL) ==="
    make -C "$SEARCH_DIR" clean
    make -C "$SEARCH_DIR" DEFS="-DN=$N -DPREFIX_LENGTH=$PL"
    mpirun --oversubscribe -n "$PROCS" "$SEARCH_DIR/dfs_search"
done
