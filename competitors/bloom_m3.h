#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

// Lock-free Bloom filter for HybridPGMLIPPM3.
//
// Words are std::atomic<uint64_t> so foreground reads (MaybeContains) and
// foreground writes (Add) on the active buffer never race with the worker's
// Reset of the flushing buffer's bloom. Reset writes 0 to each word
// atomically; readers see either the pre-Reset bit pattern or zeros, which
// is safe because:
//   - Reset only runs after the corresponding DPGM has been Cleared (and
//     after the worker's InsertBatch has already moved those keys into LIPP).
//   - A false negative from a half-zeroed bloom causes the foreground to
//     skip the DPGM probe and fall through to LIPP -- which already has the
//     key by that point.
class BloomM3 {
 public:
  BloomM3() = default;

  // Called once during Build; not concurrent with anything.
  void Init(size_t expected_keys, size_t bits_per_key) {
    if (expected_keys == 0) expected_keys = 1;
    size_t bits = expected_keys * bits_per_key;
    bits = (bits + 63) & ~size_t{63};
    int hashes = static_cast<int>((bits_per_key * 69 + 50) / 100);
    if (hashes < 1) hashes = 1;
    num_words_ = bits / 64;
    bits_ = bits;
    num_hashes_ = hashes;
    words_ = std::make_unique<std::atomic<uint64_t>[]>(num_words_);
    for (size_t i = 0; i < num_words_; ++i) {
      words_[i].store(0, std::memory_order_release);
    }
  }

  // Worker-side: zero all words atomically. May run concurrently with
  // foreground MaybeContains; see class comment for why that's safe.
  void Reset() {
    for (size_t i = 0; i < num_words_; ++i) {
      words_[i].store(0, std::memory_order_release);
    }
  }

  // Foreground-side on the active buffer. Single-threaded with respect to
  // Reset (Reset is only called on the *other* buffer's bloom).
  void Add(uint64_t key) {
    if (bits_ == 0) return;
    uint64_t h1 = Hash1(key);
    uint64_t h2 = Hash2(key);
    for (int i = 0; i < num_hashes_; ++i) {
      uint64_t pos = (h1 + uint64_t(i) * h2) % bits_;
      words_[pos >> 6].fetch_or(uint64_t{1} << (pos & 63),
                                 std::memory_order_release);
    }
  }

  bool MaybeContains(uint64_t key) const {
    if (bits_ == 0) return false;
    uint64_t h1 = Hash1(key);
    uint64_t h2 = Hash2(key);
    for (int i = 0; i < num_hashes_; ++i) {
      uint64_t pos = (h1 + uint64_t(i) * h2) % bits_;
      if (!(words_[pos >> 6].load(std::memory_order_acquire) &
            (uint64_t{1} << (pos & 63)))) {
        return false;
      }
    }
    return true;
  }

  size_t SizeBytes() const { return num_words_ * sizeof(uint64_t); }

 private:
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

  std::unique_ptr<std::atomic<uint64_t>[]> words_;
  size_t num_words_ = 0;
  size_t bits_ = 0;
  int num_hashes_ = 6;
};
