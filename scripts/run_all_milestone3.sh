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

echo "Step 4: Building benchmarks (baseline + m3)..."
./scripts/build_benchmark.sh

echo "Step 5: Running Milestone 3 benchmarks (LIPP, DPGM, M2 hybrid, M3 hybrid; all datasets x mixed workloads)..."
./scripts/run_milestone3.sh

echo "=== Milestone 3 Benchmark completed successfully ==="
echo "Check results in the 'results_m3_final' directory."
