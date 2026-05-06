#!/bin/bash
set -e

# Run this on the LOGIN node (not via sbatch).
# Compute nodes don't have cmake/make available.
# After this finishes, submit the sbatch job that runs run_all_milestone3.sh.

echo "=== Milestone 3 prep (login-node only) ==="

if ! command -v cmake >/dev/null 2>&1; then
    echo "ERROR: cmake not found in PATH. Are you on the login node?"
    echo "       module load cmake     # if needed"
    exit 1
fi

echo "Step 1: Downloading dataset (if missing)..."
./scripts/download_dataset.sh

echo "Step 2: Creating Milestone 3 CMakeLists.txt (-march=native)..."
./scripts/create_minimal_cmake_milestone3.sh

echo "Step 3: Generating workload files..."
./scripts/generate_workloads.sh

echo "Step 4a: Clearing build/ for a clean rebuild..."
rm -rf build

echo "Step 4b: Building benchmark + benchmark_m3..."
./scripts/build_benchmark.sh

echo
echo "=== Build complete ==="
echo "Now submit: sbatch <your_sbatch_script>     # which runs scripts/run_all_milestone3.sh"
