#!/usr/bin/env bash

# Sweeps the HybridPGMLIPPM3 flush threshold across a small grid.
# Threshold is set via HYBRID_FLUSH_PERMILLE (parts per thousand of N).
# Grid covers sub-flush and multi-flush regimes for the 2M-op workloads:
#   5  permille = 0.5% = 500K keys (3 flushes during 1.8M-insert workload)
#   10 permille = 1.0% = 1M  keys  (1 flush)
#   20 permille = 2.0% = 2M  keys  (no flush during workload)
#   50 permille = 5.0% = 5M  keys  (no flush; matches M3 default and M2)
# Output CSVs land in results_m3_sweep/<permille>/.

echo "Executing HybridPGMLIPPM3 flush-threshold sweep..."

BENCHMARK=build/benchmark_m3
if [ ! -f $BENCHMARK ]; then
    echo "benchmark_m3 binary does not exist. Run scripts/create_minimal_cmake_milestone3.sh and scripts/build_benchmark.sh first."
    exit 1
fi

DATASETS=(
    "fb_100M_public_uint64"
    "books_100M_public_uint64"
    "osmc_100M_public_uint64"
)

PERMILLES=(5 10 20 50)

mkdir -p ./results

run_one () {
    local DATA=$1
    local OPS=$2
    echo "  [permille=${HYBRID_FLUSH_PERMILLE}] [HybridPGMLIPPM3] ${OPS}"
    $BENCHMARK ./data/${DATA} $OPS --through --csv --only HybridPGMLIPPM3 -r 3
}

for PERMILLE in "${PERMILLES[@]}"; do
    export HYBRID_FLUSH_PERMILLE=${PERMILLE}
    OUT=./results_m3_sweep/permille_${PERMILLE}
    mkdir -p $OUT

    for DATA in "${DATASETS[@]}"; do
        if [ ! -f "./data/${DATA}" ]; then
            echo "Skipping ${DATA} (data file not present)."
            continue
        fi
        INSERT_HEAVY=./data/${DATA}_ops_2M_0.000000rq_0.500000nl_0.900000i_0m_mix
        LOOKUP_HEAVY=./data/${DATA}_ops_2M_0.000000rq_0.500000nl_0.100000i_0m_mix

        run_one ${DATA} ${LOOKUP_HEAVY}
        run_one ${DATA} ${INSERT_HEAVY}
    done

    mv ./results/*_mix_results_table.csv $OUT/ 2>/dev/null || true

    for FILE in $OUT/*.csv; do
        [ -f "$FILE" ] || continue
        if head -n 1 "$FILE" | grep -q "index_name"; then
            sed -i '1d' "$FILE"
        fi
        sed -i '1s/^/index_name,build_time_ns1,build_time_ns2,build_time_ns3,index_size_bytes,mixed_throughput_mops1,mixed_throughput_mops2,mixed_throughput_mops3,search_method,value\n/' "$FILE"
    done
done

unset HYBRID_FLUSH_PERMILLE

echo "===================Hybrid threshold sweep complete!===================="
echo "Per-threshold CSVs in ./results_m3_sweep/permille_<N>/"
