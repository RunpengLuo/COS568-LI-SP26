#!/usr/bin/env bash

echo "Executing Milestone 2 benchmarks..."

BENCHMARK=build/benchmark_m2
if [ ! -f $BENCHMARK ]; then
    echo "benchmark_m2 binary does not exist. Run scripts/create_minimal_cmake_milestone2.sh and scripts/build_benchmark.sh first."
    exit 1
fi

mkdir -p ./results_m2

DATA=fb_100M_public_uint64
INSERT_HEAVY=./data/${DATA}_ops_2M_0.000000rq_0.500000nl_0.900000i_0m_mix
LOOKUP_HEAVY=./data/${DATA}_ops_2M_0.000000rq_0.500000nl_0.100000i_0m_mix

run_one () {
    echo "  [${2}] ${1}"
    $BENCHMARK ./data/${DATA} $1 --through --csv --only $2 -r 3
}

for INDEX in LIPP DynamicPGM HybridPGMLIPP
do
    run_one ${LOOKUP_HEAVY} ${INDEX}
    run_one ${INSERT_HEAVY} ${INDEX}
done

echo "===================Milestone 2 benchmarking complete!===================="

mv ./data/*_mix.csv ./results_m2/ 2>/dev/null || true
mv ./results/*_mix.csv ./results_m2/ 2>/dev/null || true

for FILE in ./results_m2/*.csv
do
    [ -f "$FILE" ] || continue
    if head -n 1 "$FILE" | grep -q "index_name"; then
        sed -i '1d' "$FILE"
    fi
    sed -i '1s/^/index_name,build_time_ns1,build_time_ns2,build_time_ns3,index_size_bytes,mixed_throughput_mops1,mixed_throughput_mops2,mixed_throughput_mops3,search_method,value\n/' "$FILE"
    echo "Header set for $FILE"
done
