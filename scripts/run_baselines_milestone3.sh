#!/usr/bin/env bash

# Runs only the LIPP and DynamicPGM baselines across all 3 datasets x 2 mixed
# workloads x -r 3. Useful for filling in OSMC baseline numbers without
# rerunning the hybrid (and without redoing FB/Books results you already have).
# Output: results_m3_baselines/

echo "Executing Milestone 3 baseline-only benchmarks (LIPP + DynamicPGM)..."

BENCHMARK=build/benchmark_m3
if [ ! -f $BENCHMARK ]; then
    echo "benchmark_m3 binary does not exist. Run scripts/create_minimal_cmake_milestone3.sh and scripts/build_benchmark.sh first."
    exit 1
fi

mkdir -p ./results ./results_m3_baselines

DATASETS=(
    "fb_100M_public_uint64"
    "books_100M_public_uint64"
    "osmc_100M_uint64"
)

run_one () {
    local DATA=$1
    local OPS=$2
    local INDEX=$3
    echo "  [${INDEX}] ${OPS}"
    $BENCHMARK ./data/${DATA} $OPS --through --csv --only $INDEX -r 3
}

for DATA in "${DATASETS[@]}"; do
    if [ ! -f "./data/${DATA}" ]; then
        echo "Skipping ${DATA} (data file not present)."
        continue
    fi
    INSERT_HEAVY=./data/${DATA}_ops_2M_0.000000rq_0.500000nl_0.900000i_0m_mix
    LOOKUP_HEAVY=./data/${DATA}_ops_2M_0.000000rq_0.500000nl_0.100000i_0m_mix

    for INDEX in LIPP DynamicPGM; do
        run_one ${DATA} ${LOOKUP_HEAVY} ${INDEX}
        run_one ${DATA} ${INSERT_HEAVY} ${INDEX}
    done
done

echo "===================Baseline benchmarking complete!===================="

mv ./results/*_mix_results_table.csv ./results_m3_baselines/ 2>/dev/null || true

for FILE in ./results_m3_baselines/*.csv; do
    [ -f "$FILE" ] || continue
    if head -n 1 "$FILE" | grep -q "index_name"; then
        sed -i '1d' "$FILE"
    fi
    sed -i '1s/^/index_name,build_time_ns1,build_time_ns2,build_time_ns3,index_size_bytes,mixed_throughput_mops1,mixed_throughput_mops2,mixed_throughput_mops3,search_method,value\n/' "$FILE"
    echo "Header set for $FILE"
done
