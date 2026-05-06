#pragma once

#include <cstddef>
#include <cstdint>
#include <shared_mutex>
#include <vector>

class BloomM3 {
 public:
  BloomM3() = default;

  void Reset(size_t expected_keys, size_t bits_per_key = 10) {
    if (expected_keys == 0) expected_keys = 1;
    size_t bits = expected_keys * bits_per_key;
    bits = (bits + 63) & ~size_t{63};
    std::unique_lock<std::shared_mutex> guard(mu_);
    bits_ = bits;
    words_.assign(bits / 64, 0);
  }

  void Add(uint64_t key) {
    std::unique_lock<std::shared_mutex> guard(mu_);
    if (bits_ == 0) return;
    uint64_t h1 = Hash1(key);
    uint64_t h2 = Hash2(key);
    for (int i = 0; i < kHashes; ++i) {
      uint64_t pos = (h1 + uint64_t(i) * h2) % bits_;
      words_[pos >> 6] |= (uint64_t{1} << (pos & 63));
    }
  }

  bool MaybeContains(uint64_t key) const {
    std::shared_lock<std::shared_mutex> guard(mu_);
    if (bits_ == 0) return false;
    uint64_t h1 = Hash1(key);
    uint64_t h2 = Hash2(key);
    for (int i = 0; i < kHashes; ++i) {
      uint64_t pos = (h1 + uint64_t(i) * h2) % bits_;
      if (!(words_[pos >> 6] & (uint64_t{1} << (pos & 63)))) return false;
    }
    return true;
  }

  size_t SizeBytes() const {
    std::shared_lock<std::shared_mutex> guard(mu_);
    return words_.size() * sizeof(uint64_t);
  }

 private:
  static constexpr int kHashes = 6;

  static uint64_t Hash1(uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
  }

  static uint64_t Hash2(uint64_t x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x = x ^ (x >> 31);
    return x | 1;
  }

  mutable std::shared_mutex mu_;
  size_t bits_ = 0;
  std::vector<uint64_t> words_;
};
