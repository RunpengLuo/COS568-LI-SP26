#ifndef TLI_DYNAMIC_PGM_M3_H
#define TLI_DYNAMIC_PGM_M3_H

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>

#include "../util.h"
#include "base.h"
#include "pgm_index_dynamic.hpp"

// Lock-free DPGM wrapper using a seqlock to coordinate the rare
// foreground-read-vs-worker-Clear race on the *flushing* buffer.
//
// Concurrency model assumed by HybridPGMLIPPM3:
//   - The *active* buffer is touched exclusively by the single foreground
//     thread (Insert + EqualityLookup). Sequential, no race.
//   - The *flushing* buffer is mutated by the worker (Snapshot, then Clear)
//     and may be read concurrently by foreground (via ProbeBuffer fall-through).
//     The seqlock detects an in-progress Clear and forces the foreground
//     reader to bail out (return OVERFLOW). This is safe because by the time
//     Clear runs the worker has already InsertBatch'd those keys into LIPP,
//     so a foreground LIPP-first lookup will find them.
template <class KeyType, class SearchClass, size_t pgm_error>
class DynamicPGMM3 : public Competitor<KeyType, SearchClass> {
 public:
  DynamicPGMM3(const std::vector<int>& /*params*/) {}

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t /*num_threads*/) {
    std::vector<std::pair<KeyType, uint64_t>> loading;
    loading.reserve(data.size());
    for (const auto& kv : data) loading.emplace_back(kv.key, kv.value);
    return util::timing([&] {
      pgm_ = decltype(pgm_)(loading.begin(), loading.end());
    });
  }

  size_t EqualityLookup(const KeyType& key, uint32_t /*tid*/) const {
    // Seqlock optimistic read. Even generation = stable; odd = mutating.
    uint64_t v1 = generation_.load(std::memory_order_acquire);
    if (v1 & 1) return util::OVERFLOW;
    auto it = pgm_.find(key);
    size_t guess = (it == pgm_.end()) ? util::OVERFLOW : it->value();
    std::atomic_thread_fence(std::memory_order_acquire);
    uint64_t v2 = generation_.load(std::memory_order_acquire);
    if (v1 != v2) return util::OVERFLOW;
    return guess;
  }

  void Insert(const KeyValue<KeyType>& kv, uint32_t /*tid*/) {
    // Foreground-only path on the active buffer; no concurrent reader exists.
    pgm_.insert(kv.key, kv.value);
    size_t cur = size_.load(std::memory_order_relaxed);
    if (cur == 0 || kv.key < min_key_) min_key_ = kv.key;
    if (cur == 0 || kv.key > max_key_) max_key_ = kv.key;
    size_.store(cur + 1, std::memory_order_release);
  }

  size_t entry_count() const { return size_.load(std::memory_order_acquire); }
  KeyType min_key() const { return min_key_; }
  KeyType max_key() const { return max_key_; }

  void Snapshot(std::vector<std::pair<KeyType, uint64_t>>& out) const {
    // Worker-only on the flushing buffer; runs strictly before Clear and is
    // the sole accessor at this moment, so iteration is safe lock-free.
    out.clear();
    out.reserve(size_.load(std::memory_order_acquire));
    for (auto it = pgm_.lower_bound(std::numeric_limits<KeyType>::min());
         it != pgm_.end(); ++it) {
      out.emplace_back(it->key(), it->value());
    }
  }

  void Clear() {
    // Mark mutation start (odd), do the destructive write, mark stable (even).
    generation_.fetch_add(1, std::memory_order_acq_rel);
    pgm_ = decltype(pgm_)();
    size_.store(0, std::memory_order_release);
    min_key_ = std::numeric_limits<KeyType>::max();
    max_key_ = std::numeric_limits<KeyType>::min();
    generation_.fetch_add(1, std::memory_order_acq_rel);
  }

  std::string name() const { return "DynamicPGMM3"; }
  std::size_t size() const { return pgm_.size_in_bytes(); }

  bool applicable(bool /*unique*/, bool /*range*/, bool /*insert*/,
                  bool multithread, const std::string& /*ops*/) const {
    return SearchClass::name() != "LinearAVX" && !multithread;
  }

  std::vector<std::string> variants() const {
    return {SearchClass::name(), std::to_string(pgm_error)};
  }

 private:
  std::atomic<uint64_t> generation_{0};
  DynamicPGMIndex<KeyType, uint64_t, SearchClass,
                  PGMIndex<KeyType, SearchClass, pgm_error, 16>> pgm_;
  std::atomic<size_t> size_{0};
  KeyType min_key_ = std::numeric_limits<KeyType>::max();
  KeyType max_key_ = std::numeric_limits<KeyType>::min();
};

#endif
