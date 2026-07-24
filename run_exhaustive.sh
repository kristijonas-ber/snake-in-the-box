#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TRACK_ROOT="$REPO_ROOT/exhaustive"
SRC_DIR="$TRACK_ROOT/dfs_search"

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

Standalone prefix pipeline (generate once, distribute, search anywhere):
      --gen-prefixes     generate canonical prefixes to .pfx batch files (no MPI)
        --gen-out DIR      output parent dir       (default exhaustive/prefixes;
                             batches land in DIR/dim<N>_pl<PL>_batch<size>/)
        --batch-size M     prefixes per file       (default 1000000)
        --from A --to B    only ordinals [A, B)    (partition / resume)
      --from-files F...  MPI DFS over the given .pfx batch files (all args after
                         --from-files are file paths). N and PREFIX_LENGTH are
                         read from the file header, so you need not repeat
                         --dim/--prefix-length here.
      --subtree T...     exhaustively search one subtree from a prefix given as
                         transitions (all args after --subtree); no MPI, no
                         enumeration. Reports the longest snake within it.

dfs_search needs at least 2 ranks: rank 0 dispatches, the rest search.
'make clean' is forced by default because the Makefile's object rule does not
depend on DEFS, so a changed -DN would otherwise silently relink stale objects.

Examples:
  ./run_exhaustive.sh --procs 5 --oversubscribe
  ./run_exhaustive.sh --dim 8 --prefix-length 18 --procs 16
  ./run_exhaustive.sh --dim 7 --gen-prefixes --batch-size 500000
  ./run_exhaustive.sh --procs 10 --from-files exhaustive/prefixes/dim7_pl*/batch_*.pfx
  ./run_exhaustive.sh --decode ../job_outputs/snakes_dfs_search/foo.bin

Output: exhaustive/job_outputs/  (searches)   exhaustive/prefixes/  (generator)
EOF
}

defs=()
procs=5
binary="dfs_search"
oversubscribe=0
do_clean=1
decode_file=""
mode="search"
gen_out="prefixes"
batch_size=1000000
gen_from=0
gen_to=""
files=()
subtree_args=()
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
        --gen-prefixes)     mode="gen";                   shift   ;;
        --gen-out)          gen_out="$2";                 shift 2 ;;
        --batch-size)       batch_size="$2";              shift 2 ;;
        --from)             gen_from="$2";                shift 2 ;;
        --to)               gen_to="$2";                  shift 2 ;;
        --from-files)       mode="files";                 shift; files=("$@"); break ;;
        --subtree)          mode="subtree";               shift; subtree_args=("$@"); break ;;
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

# Resolve any --from-files paths to absolute before we change directory.
if [ "$mode" = "files" ]; then
    if [ "${#files[@]}" -eq 0 ]; then
        echo "run_exhaustive.sh: --from-files needs at least one .pfx file" >&2
        exit 2
    fi
    abs_files=()
    for fpath in "${files[@]}"; do
        if [ ! -f "$fpath" ]; then
            echo "run_exhaustive.sh: no such file: $fpath" >&2
            exit 2
        fi
        abs_files+=("$(cd "$(dirname "$fpath")" && pwd)/$(basename "$fpath")")
    done

    # The .pfx header stores N and PREFIX_LENGTH; build to match the file so you
    # never have to repeat --dim/--prefix-length from the generate step. Header:
    # magic 'P''F''X''1' (80 70 88 49), then u16 N, u16 PREFIX_LENGTH (LE).
    read -r m0 m1 m2 m3 n_lo n_hi pl_lo pl_hi \
        <<< "$(od -An -tu1 -j0 -N8 "${abs_files[0]}")"
    if [ "$m0 $m1 $m2 $m3" != "80 70 88 49" ]; then
        echo "run_exhaustive.sh: ${abs_files[0]} is not a .pfx file" >&2
        exit 2
    fi
    file_n=$(( n_lo + 256 * n_hi ))
    file_pl=$(( pl_lo + 256 * pl_hi ))

    # File dictates N and PREFIX_LENGTH: drop any that were passed, then set them.
    kept=()
    dropped=0
    for d in ${defs[@]+"${defs[@]}"}; do
        case "$d" in
            -DN=*|-DPREFIX_LENGTH=*) dropped=1 ;;
            *)                       kept+=("$d") ;;
        esac
    done
    defs=(${kept[@]+"${kept[@]}"} "-DN=$file_n" "-DPREFIX_LENGTH=$file_pl")

    echo "+ from-files: batch header says N=$file_n PREFIX_LENGTH=$file_pl (building to match)" >&2
    [ "$dropped" -eq 1 ] && \
        echo "  (a --dim/--prefix-length you passed was overridden by the file's values)" >&2
fi

if [ "$do_clean" -eq 1 ]; then
    echo "+ make -C exhaustive/dfs_search clean" >&2
    make -C "$SRC_DIR" clean >&2
fi

echo "+ make -C exhaustive/dfs_search DEFS=\"${defs[*]-}\"" >&2
make -C "$SRC_DIR" DEFS="${defs[*]-}" >&2

cd "$TRACK_ROOT"

# --- decode ----------------------------------------------------------------
if [ -n "$decode_file" ]; then
    echo "+ (cd exhaustive && dfs_search/dfs_search --decode $decode_file)" >&2
    exec dfs_search/dfs_search --decode "$decode_file"
fi

# --- generate prefixes to .pfx files (no MPI) ------------------------------
if [ "$mode" = "gen" ]; then
    to_arg="$gen_to"
    if [ -z "$to_arg" ]; then to_arg="18446744073709551615"; fi   # ULLONG_MAX = all
    echo "+ (cd exhaustive && dfs_search/prefixgen_tool $gen_out $batch_size $gen_from $to_arg)" >&2
    exec dfs_search/prefixgen_tool "$gen_out" "$batch_size" "$gen_from" "$to_arg"
fi

# --- exhaustive search of one subtree from a prefix (no MPI) ---------------
if [ "$mode" = "subtree" ]; then
    echo "+ (cd exhaustive && dfs_search/subtree_search ${subtree_args[*]-})" >&2
    exec dfs_search/subtree_search ${subtree_args[@]+"${subtree_args[@]}"}
fi

# --- MPI DFS over prefix batch files ---------------------------------------
if [ "$mode" = "files" ]; then
    cmd=(mpirun)
    [ "$oversubscribe" -eq 1 ] && cmd+=(--oversubscribe)
    cmd+=(-n "$procs" dfs_search/dfs_from_files "${abs_files[@]}")
    echo "+ (cd exhaustive && ${cmd[*]})" >&2
    exec "${cmd[@]}"
fi

# --- normal MPI search -----------------------------------------------------
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
cmd+=("dfs_search/$binary")

echo "+ (cd exhaustive && ${cmd[*]})" >&2
exec "${cmd[@]}"
