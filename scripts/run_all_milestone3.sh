#!/bin/bash
set -e

# This script is intended to be invoked from a sbatch job.
# Prereq: run scripts/build_milestone3.sh on the LOGIN node first
# (compute nodes do not have cmake/make).

echo "=== Milestone 3 benchmark run (sbatch) ==="

chmod +x scripts/*.sh

if [ ! -f build/benchmark_m3 ]; then
    echo "ERROR: build/benchmark_m3 missing."
    echo "       Run scripts/build_milestone3.sh on the login node first."
    exit 1
fi

echo "Step 1: Running baselines + M3 hybrid (canonical best params)..."
./scripts/run_milestone3.sh

echo "Step 2: Running M3 hybrid (Bloom bits x flush threshold) sweep..."
./scripts/run_hybrid_param_sweep.sh

echo
echo "=== Milestone 3 benchmark run complete ==="
echo "Output:"
echo "  ./results_m3_final/                                 -- baselines + canonical M3"
echo "  ./results_m3_param_sweep/bits_<B>_permille_<P>/     -- M3 hybrid swept"
