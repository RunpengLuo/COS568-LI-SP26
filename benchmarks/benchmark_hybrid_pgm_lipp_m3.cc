#include "benchmarks/benchmark_hybrid_pgm_lipp_m3.h"

#include "benchmark.h"
#include "common.h"
#include "competitors/hybrid_pgm_lipp_m3.h"

void benchmark_64_hybrid_pgm_lipp_m3(tli::Benchmark<uint64_t>& benchmark) {
  benchmark.template Run<HybridPGMLIPPM3<uint64_t>>();
}
