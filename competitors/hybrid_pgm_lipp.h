#pragma once

#include <vector>

#include "../util.h"
#include "base.h"
#include "dynamic_pgm_index_m2.h"
#include "lipp.h"
#include "searches/branching_binary_search.h"

template <class KeyType>
class HybridPGMLIPP : public Base<KeyType> {
 public:
  HybridPGMLIPP(const std::vector<int>& params)
      : dpgm_(params), lipp_(params) {}

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    flush_threshold_ = std::max<size_t>(1, data.size() / 20);
    return lipp_.Build(data, num_threads);
  }

  size_t EqualityLookup(const KeyType& lookup_key, uint32_t thread_id) const {
    size_t v = dpgm_.EqualityLookup(lookup_key, thread_id);
    if (v != util::OVERFLOW) return v;
    return lipp_.EqualityLookup(lookup_key, thread_id);
  }

  void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
    dpgm_.Insert(data, thread_id);
    if (++pgm_count_ >= flush_threshold_) {
      Flush(thread_id);
    }
  }

  std::string name() const { return "HybridPGMLIPP"; }

  std::size_t size() const { return dpgm_.size() + lipp_.size(); }

  bool applicable(bool unique, bool range_query, bool insert, bool multithread,
                  const std::string& ops_filename) const {
    return unique && !multithread;
  }

  std::vector<std::string> variants() const {
    return std::vector<std::string>();
  }

 private:
  void Flush(uint32_t thread_id) {
    for (auto it = dpgm_.begin(); it != dpgm_.end(); ++it) {
      lipp_.Insert(KeyValue<KeyType>{it->key(), it->value()}, thread_id);
    }
    dpgm_.clear();
    pgm_count_ = 0;
  }

  DynamicPGMM2<KeyType, BranchingBinarySearch<0>, 64> dpgm_;
  Lipp<KeyType> lipp_;

  size_t flush_threshold_ = 0;
  size_t pgm_count_ = 0;
};
