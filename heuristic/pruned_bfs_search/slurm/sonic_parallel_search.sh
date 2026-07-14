#!/bin/bash -l
# sonic_parallel_search.sh - Run the OpenMP parallel heuristic snake search on
# Sonic HPC via SLURM.
#
# parallel_search is a shared-memory (OpenMP) program, so it runs on a SINGLE
# node and scales across the cores of that node. We therefore request 1 node
# (-N 1), 1 task (-n 1), and many CPUs for that task (-c), then hand that core
# count to OpenMP via OMP_NUM_THREADS.
#
# Submit:   sbatch sonic_parallel_search.sh
# Monitor:  squeue -u $USER      Cancel: scancel <jobid>
# Details:  sacct -j <jobid>

#SBATCH --job-name=snake_parallel
#SBATCH -N 1                     # one node (shared-memory OpenMP)
#SBATCH -n 1                     # one task...
#SBATCH -c 16                    # ...with 16 CPU cores (shared user max is 48)
#SBATCH --mem=0G                # RAM reserved on the node (job killed if exceeded)
#SBATCH -t 24:00:00              # walltime hh:mm:ss (standard queue max 10 days)
#SBATCH --mail-type=ALL
#SBATCH --mail-user=kristijonas.bernatonis@gmail.com
#SBATCH -o snake_parallel_%j.out # stdout -> file tagged with the job id

# --- Parameters (edit these) -------------------------------------------------
DIMENSION=14
# Keep MEMORY_GB a few GB UNDER --mem so the search prunes before it fills the
# reservation - that is what keeps it in RAM and off swap/SSD.
MEMORY_GB=18.0
# ----------------------------------------------------------------------------

cd "${SLURM_SUBMIT_DIR:-$PWD}"

# Locate the repo root by walking up until run_heuristic.sh is found, so this
# works no matter which directory inside the repo you sbatch from.
REPO_ROOT="$PWD"
while [ ! -f "$REPO_ROOT/run_heuristic.sh" ] && [ "$REPO_ROOT" != "/" ]; do
    REPO_ROOT="$(dirname "$REPO_ROOT")"
done
if [ ! -f "$REPO_ROOT/run_heuristic.sh" ]; then
    echo "Could not find run_heuristic.sh above ${SLURM_SUBMIT_DIR:-$PWD}." >&2
    echo "Submit this script from inside the snake-in-the-box repo." >&2
    exit 1
fi

# Log what this run actually got, so you can right-size next time.
echo "=== Resources for job $SLURM_JOB_ID on $(hostname) ==="
echo "Cores allocated : ${SLURM_CPUS_PER_TASK:-?}"
echo "Memory reserved : ${SLURM_MEM_PER_NODE:-?} MB"
echo "Node RAM (free) :"; free -h
echo "======================================================="

# Load a recent GCC (provides __builtin_* intrinsics + OpenMP). Adjust the
# version to whatever `module avail gcc` shows on Sonic.
module load gcc || true

# One OpenMP thread per allocated core.
export OMP_NUM_THREADS=${SLURM_CPUS_PER_TASK:-16}
echo "Running dimension $DIMENSION on $OMP_NUM_THREADS threads"

# run_heuristic.sh builds parallel_search and runs it from heuristic/, so the
# snakes it finds land in heuristic/snakes/ and heuristic/seeds/.
stdbuf -oL "$REPO_ROOT/run_heuristic.sh" parallel "$DIMENSION" "$MEMORY_GB" "$OMP_NUM_THREADS"
