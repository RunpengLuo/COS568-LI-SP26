#!/bin/bash
set -e

echo "=== Starting Milestone 3 Benchmark ==="

chmod +x scripts/*.sh

echo "Step 1: Downloading dataset..."
./scripts/download_dataset.sh

echo "Step 2: Creating Milestone 3 CMakeLists.txt..."
./scripts/create_minimal_cmake_milestone3.sh

echo "Step 3: Generating workloads..."
./scripts/generate_workloads.sh

# Force a clean build so any change to compile flags (-march, etc.) in
# the cmake script actually applies. Incremental builds reuse stale .o
# files compiled with the previous flags.
echo "Step 4a: Clearing build/ for a clean rebuild..."
rm -rf build

echo "Step 4b: Building benchmarks (baseline + m3)..."
./scripts/build_benchmark.sh

echo "Step 5: Running Milestone 3 benchmarks (LIPP, DPGM, M2 hybrid, M3 hybrid; all datasets x mixed workloads)..."
./scripts/run_milestone3.sh

echo "=== Milestone 3 Benchmark completed successfully ==="
echo "Final canonical results: ./results_m3_final/"
echo
echo "Optional: hyperparameter sweep over (Bloom bits x flush threshold):"
echo "  bash ./scripts/run_hybrid_param_sweep.sh"
echo "  Output: ./results_m3_param_sweep/bits_<B>_permille_<P>/"
