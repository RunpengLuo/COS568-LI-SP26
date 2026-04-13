#!/bin/bash
set -e

echo "=== Starting Milestone 2 Benchmark ==="

chmod +x scripts/*.sh

echo "Step 1: Downloading dataset..."
./scripts/download_dataset.sh

echo "Step 2: Creating Milestone 2 CMakeLists.txt..."
./scripts/create_minimal_cmake_milestone2.sh

echo "Step 3: Generating workloads..."
./scripts/generate_workloads.sh

echo "Step 4: Building benchmarks (baseline + m2)..."
./scripts/build_benchmark.sh

echo "Step 5: Running Milestone 2 benchmarks (fb_100M mixed workloads)..."
./scripts/run_benchmarks_milestone2.sh

echo "=== Milestone 2 Benchmark completed successfully ==="
echo "Check results in the 'results_m2' directory."
