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

# Force a clean build so any change to compile flags (-march=native, etc.)
# in the cmake script actually applies. Incremental builds reuse stale .o
# files compiled with the previous flags.
echo "Step 4a: Clearing build/ for a clean rebuild..."
rm -rf build

echo "Step 4b: Building benchmarks (baseline + m3)..."
./scripts/build_benchmark.sh

echo "Step 5: Running baselines + M3 hybrid (canonical best params)..."
./scripts/run_milestone3.sh

echo "Step 6: Running M3 hybrid (Bloom bits x flush threshold) sweep..."
./scripts/run_hybrid_param_sweep.sh

echo "=== Milestone 3 Benchmark completed successfully ==="
echo
echo "Output:"
echo "  ./results_m3_final/                       -- baselines + M3 hybrid at default best params"
echo "  ./results_m3_param_sweep/bits_<B>_permille_<P>/   -- M3 hybrid swept across params"
echo
echo "After the run, pick the best (bits, permille) per workload from the"
echo "sweep and compare against the LIPP/DPGM/M2-hybrid baselines in"
echo "results_m3_final/."
