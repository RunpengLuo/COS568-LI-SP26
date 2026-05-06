#pragma once

#include <shared_mutex>
#include <utility>
#include <vector>

#include "./lipp/src/core/lipp.h"
#include "base.h"

template <class KeyType>
class LippM3 : public Base<KeyType> {
 public:
  LippM3(const std::vector<int>& /*params*/) {}

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t /*num_threads*/) {
    std::vector<std::pair<KeyType, uint64_t>> loading;
    loading.reserve(data.size());
    for (const auto& kv : data) loading.emplace_back(kv.key, kv.value);
    return util::timing([&] { lipp_.bulk_load(loading.data(), loading.size()); });
  }

  size_t EqualityLookup(const KeyType& key, uint32_t /*tid*/) const {
    std::shared_lock<std::shared_mutex> guard(mu_);
    uint64_t value;
    if (!lipp_.find(key, value)) return util::NOT_FOUND;
    return value;
  }

  void Insert(const KeyValue<KeyType>& kv, uint32_t /*tid*/) {
    std::unique_lock<std::shared_mutex> guard(mu_);
    lipp_.insert(kv.key, kv.value);
  }

  void InsertBatch(const std::vector<std::pair<KeyType, uint64_t>>& batch) {
    std::unique_lock<std::shared_mutex> guard(mu_);
    for (const auto& [k, v] : batch) lipp_.insert(k, v);
  }

  std::string name() const { return "LippM3"; }
  std::size_t size() const { return lipp_.index_size(); }

  bool applicable(bool unique, bool /*range*/, bool /*insert*/,
                  bool multithread, const std::string& /*ops*/) const {
    return unique && !multithread;
  }

  std::vector<std::string> variants() const { return {}; }

 private:
  mutable std::shared_mutex mu_;
  LIPP<KeyType, uint64_t> lipp_;
};
