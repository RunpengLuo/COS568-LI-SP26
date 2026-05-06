#!/usr/bin/env bash

# Single canonical Milestone 3 run.
#
# Runs the four indexes against both mixed workloads on all three datasets,
# producing exactly the data needed for the 12 report bar plots
# (3 datasets x 2 workloads x [throughput + index size]).
#
# Indexes:
#   LIPP             -- baseline
#   DynamicPGM       -- baseline (framework iterates its own variant sweep)
#   HybridPGMLIPP    -- Milestone 2 hybrid (synchronous, key-by-key flush)
#   HybridPGMLIPPM3  -- Milestone 3 hybrid following the README's prescribed
#                       design: all inserts buffer in DPGM, lookup probes
#                       DPGM (Bloom-guarded) before falling through to LIPP,
#                       async double-buffered flush at threshold. Per-workload
#                       best params from the (bits x permille) sweep:
#                         lookup-heavy : bits=6, permille=5
#                         insert-heavy : bits=6, permille=20
#
# Output: results_m3_final/*.csv (one CSV per dataset x workload, all 4
# indexes' rows in each).
#
# Wall-clock: ~45-60 min on Adroit (24 binary invocations; DPGM's internal
# variant sweep dominates).

echo "Executing Milestone 3 final canonical run..."

BENCHMARK=build/benchmark_m3
if [ ! -f $BENCHMARK ]; then
    echo "benchmark_m3 binary does not exist."
    echo "Run scripts/create_minimal_cmake_milestone3.sh and scripts/build_benchmark.sh first."
    exit 1
fi

mkdir -p ./results ./results_m3_final

DATASETS=(
    "fb_100M_public_uint64"
    "books_100M_public_uint64"
    "osmc_100M_public_uint64"
)

# M3 hybrid best params (from the (bits x permille) sweep, LIPP-first lookup).
M3_BITS=6
M3_PERMILLE_LOOKUP=5
M3_PERMILLE_INSERT=20

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

    echo "=== Dataset: ${DATA} ==="

    # Baselines and M2 hybrid (default config; no env vars needed).
    unset HYBRID_BLOOM_BITS HYBRID_FLUSH_PERMILLE
    for INDEX in LIPP DynamicPGM HybridPGMLIPP; do
        run_one ${DATA} ${LOOKUP_HEAVY} ${INDEX}
        run_one ${DATA} ${INSERT_HEAVY} ${INDEX}
    done

    # M3 hybrid: per-workload sweep-best params.
    export HYBRID_BLOOM_BITS=${M3_BITS}

    export HYBRID_FLUSH_PERMILLE=${M3_PERMILLE_LOOKUP}
    echo "  M3 lookup-heavy: bits=${HYBRID_BLOOM_BITS}, permille=${HYBRID_FLUSH_PERMILLE}"
    run_one ${DATA} ${LOOKUP_HEAVY} HybridPGMLIPPM3

    export HYBRID_FLUSH_PERMILLE=${M3_PERMILLE_INSERT}
    echo "  M3 insert-heavy: bits=${HYBRID_BLOOM_BITS}, permille=${HYBRID_FLUSH_PERMILLE}"
    run_one ${DATA} ${INSERT_HEAVY} HybridPGMLIPPM3

    unset HYBRID_BLOOM_BITS HYBRID_FLUSH_PERMILLE
done

echo "===================Milestone 3 final run complete!===================="

mv ./results/*_mix_results_table.csv ./results_m3_final/ 2>/dev/null || true

for FILE in ./results_m3_final/*.csv; do
    [ -f "$FILE" ] || continue
    if head -n 1 "$FILE" | grep -q "index_name"; then
        sed -i '1d' "$FILE"
    fi
    sed -i '1s/^/index_name,build_time_ns1,build_time_ns2,build_time_ns3,index_size_bytes,mixed_throughput_mops1,mixed_throughput_mops2,mixed_throughput_mops3,search_method,value\n/' "$FILE"
    echo "Header set for $FILE"
done

echo "Final CSVs in ./results_m3_final/"
