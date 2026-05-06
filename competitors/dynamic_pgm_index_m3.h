#ifndef TLI_DYNAMIC_PGM_M3_H
#define TLI_DYNAMIC_PGM_M3_H

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <limits>
#include <shared_mutex>
#include <utility>
#include <vector>

#include "../util.h"
#include "base.h"
#include "pgm_index_dynamic.hpp"

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
    std::shared_lock<std::shared_mutex> guard(mu_);
    auto it = pgm_.find(key);
    return it == pgm_.end() ? util::OVERFLOW : it->value();
  }

  void Insert(const KeyValue<KeyType>& kv, uint32_t /*tid*/) {
    std::unique_lock<std::shared_mutex> guard(mu_);
    pgm_.insert(kv.key, kv.value);
    if (size_ == 0 || kv.key < min_key_) min_key_ = kv.key;
    if (size_ == 0 || kv.key > max_key_) max_key_ = kv.key;
    ++size_;
  }

  size_t entry_count() const { return size_.load(std::memory_order_acquire); }
  KeyType min_key() const { return min_key_; }
  KeyType max_key() const { return max_key_; }

  void Snapshot(std::vector<std::pair<KeyType, uint64_t>>& out) const {
    std::shared_lock<std::shared_mutex> guard(mu_);
    out.clear();
    out.reserve(size_.load());
    for (auto it = pgm_.lower_bound(std::numeric_limits<KeyType>::min());
         it != pgm_.end(); ++it) {
      out.emplace_back(it->key(), it->value());
    }
  }

  void Clear() {
    std::unique_lock<std::shared_mutex> guard(mu_);
    pgm_ = decltype(pgm_)();
    size_.store(0, std::memory_order_release);
    min_key_ = std::numeric_limits<KeyType>::max();
    max_key_ = std::numeric_limits<KeyType>::min();
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
  mutable std::shared_mutex mu_;
  DynamicPGMIndex<KeyType, uint64_t, SearchClass,
                  PGMIndex<KeyType, SearchClass, pgm_error, 16>> pgm_;
  std::atomic<size_t> size_{0};
  KeyType min_key_ = std::numeric_limits<KeyType>::max();
  KeyType max_key_ = std::numeric_limits<KeyType>::min();
};

#endif
