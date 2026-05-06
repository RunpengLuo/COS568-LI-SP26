#!/usr/bin/env bash

# Sweeps both HybridPGMLIPPM3 hyperparameters:
#   HYBRID_BLOOM_BITS     — bits per key in the Bloom filter
#                           (hash count auto-derived as ~ln(2) * bits)
#   HYBRID_FLUSH_PERMILLE — flush threshold in parts per thousand of N
#
# Grid is intentionally small for tractable wall-clock:
#   bloom bits   ∈ {6, 10, 16}      — half / baseline / double precision
#   permille     ∈ {5, 20, 50}      — multi-flush / sweet-spot / default
# = 9 configs × 3 datasets × 2 workloads × -r 3 ≈ 60 min on Adroit.
#
# Output: results_m3_param_sweep/bits_<B>_permille_<P>/

echo "Executing HybridPGMLIPPM3 (bloom × threshold) parameter sweep..."

BENCHMARK=build/benchmark_m3
if [ ! -f $BENCHMARK ]; then
    echo "benchmark_m3 binary does not exist. Run scripts/create_minimal_cmake_milestone3.sh and scripts/build_benchmark.sh first."
    exit 1
fi

BLOOM_BITS=(6 10 16)
PERMILLES=(5 20 50)

DATASETS=(
    "fb_100M_public_uint64"
    "books_100M_public_uint64"
    "osmc_100M_public_uint64"
)

mkdir -p ./results

run_one () {
    local DATA=$1
    local OPS=$2
    echo "  [bits=${HYBRID_BLOOM_BITS}, permille=${HYBRID_FLUSH_PERMILLE}] [HybridPGMLIPPM3] ${OPS}"
    $BENCHMARK ./data/${DATA} $OPS --through --csv --only HybridPGMLIPPM3 -r 3
}

for BITS in "${BLOOM_BITS[@]}"; do
    for PERMILLE in "${PERMILLES[@]}"; do
        export HYBRID_BLOOM_BITS=${BITS}
        export HYBRID_FLUSH_PERMILLE=${PERMILLE}
        OUT=./results_m3_param_sweep/bits_${BITS}_permille_${PERMILLE}
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
done

unset HYBRID_BLOOM_BITS HYBRID_FLUSH_PERMILLE

echo "===================Hybrid parameter sweep complete!===================="
echo "Per-config CSVs in ./results_m3_param_sweep/bits_<B>_permille_<P>/"
echo "CSV row encoding: search_method = 'async_bloom_b<B>', value = '<P>' (permille)"
