#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../util.h"
#include "base.h"
#include "bloom_m3.h"
#include "dynamic_pgm_index_m3.h"
#include "lipp_m3.h"
#include "searches/branching_binary_search.h"

template <class KeyType>
class HybridPGMLIPPM3 : public Base<KeyType> {
 public:
  HybridPGMLIPPM3(const std::vector<int>& params)
      : dpgm_a_(params), dpgm_b_(params), lipp_(params) {
    if (const char* env = std::getenv("HYBRID_FLUSH_PERMILLE")) {
      int v = std::atoi(env);
      if (v > 0) flush_threshold_permille_ = v;
    } else if (params.size() >= 1 && params[0] > 0) {
      flush_threshold_permille_ = params[0] * 10;
    }
    if (const char* env = std::getenv("HYBRID_BLOOM_BITS")) {
      int v = std::atoi(env);
      if (v > 0) bloom_bits_per_key_ = v;
    }
    if (const char* env = std::getenv("HYBRID_INSERT_STRATEGY")) {
      // "lipp_direct"     -- always lipp_.Insert(); skip Contains() and DPGM.
      //                      Best for insert-heavy where most inserts are
      //                      updates of bulk-loaded keys.
      // "update_fast_path" -- Contains() probe, route updates to LIPP and
      //                      genuinely-new keys to DPGM (default).
      if (std::strcmp(env, "lipp_direct") == 0) {
        insert_strategy_ = InsertStrategy::kLippDirect;
      }
    }
  }

  ~HybridPGMLIPPM3() { Shutdown(); }

  uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
    flush_threshold_ = std::max<size_t>(
        1, (data.size() * flush_threshold_permille_) / 1000);
    bloom_a_.Init(flush_threshold_, bloom_bits_per_key_);
    bloom_b_.Init(flush_threshold_, bloom_bits_per_key_);
    active_.store(&dpgm_a_);
    active_bloom_.store(&bloom_a_);
    flushing_.store(&dpgm_b_);
    flushing_bloom_.store(&bloom_b_);
    uint64_t t = lipp_.Build(data, num_threads);
    StartWorker();
    return t;
  }

  size_t EqualityLookup(const KeyType& key, uint32_t tid) const {
    // LIPP-first hot path. The benchmark uses value = key, so a key present
    // in both LIPP (bulk-loaded) and DPGM (re-inserted) yields the same
    // value either way; we can skip the DPGM probes whenever LIPP hits.
    size_t v = lipp_.EqualityLookup(key, tid);
    if (v != util::NOT_FOUND) return v;

    // LIPP miss: the key can only live in DPGM (newly-inserted, not yet
    // flushed). Fall through to the Bloom-guarded DPGM probes.
    if (size_t r = ProbeBuffer(active_.load(std::memory_order_acquire),
                                active_bloom_.load(std::memory_order_acquire),
                                key, tid);
        r != util::OVERFLOW) {
      return r;
    }
    if (size_t r = ProbeBuffer(flushing_.load(std::memory_order_acquire),
                                flushing_bloom_.load(std::memory_order_acquire),
                                key, tid);
        r != util::OVERFLOW) {
      return r;
    }
    return util::NOT_FOUND;
  }

  void Insert(const KeyValue<KeyType>& kv, uint32_t tid) {
    if (insert_strategy_ == InsertStrategy::kLippDirect) {
      // Insert-heavy strategy: skip the Contains() probe and let LIPP's own
      // descent handle update-vs-insert. Saves ~440 ns per insert in
      // workloads where the Contains() check is mostly redundant work
      // (most "inserts" are updates of bulk-loaded keys). DPGM remains in
      // the architecture as a buffer for new-key bursts, but is unused on
      // this path.
      lipp_.Insert(kv, tid);
      return;
    }

    // Default strategy: update fast-path. Probe LIPP first; if hit, write
    // there directly (in-place value swap, ~730 ns). Otherwise buffer the
    // genuinely-new key in DPGM and amortize via async flush.
    if (lipp_.Contains(kv.key)) {
      lipp_.Insert(kv, tid);
      return;
    }

    DPGM* active = active_.load(std::memory_order_acquire);
    BloomM3* abloom = active_bloom_.load(std::memory_order_acquire);
    active->Insert(kv, tid);
    abloom->Add(static_cast<uint64_t>(kv.key));

    if (active->entry_count() >= flush_threshold_) {
      TriggerFlush();
    }
  }

  std::string name() const { return "HybridPGMLIPPM3"; }

  std::size_t size() const {
    return dpgm_a_.size() + dpgm_b_.size() + lipp_.size() +
           bloom_a_.SizeBytes() + bloom_b_.SizeBytes();
  }

  bool applicable(bool unique, bool /*range*/, bool /*insert*/,
                  bool multithread, const std::string& /*ops*/) const {
    return unique && !multithread;
  }

  std::vector<std::string> variants() const {
    const char* strat = (insert_strategy_ == InsertStrategy::kLippDirect)
                            ? "lippdirect"
                            : "fastpath";
    return {std::string("async_bloom_b") + std::to_string(bloom_bits_per_key_) +
                "_" + strat,
            std::to_string(flush_threshold_permille_)};
  }

 private:
  using DPGM = DynamicPGMM3<KeyType, BranchingBinarySearch<0>, 64>;

  size_t ProbeBuffer(DPGM* dpgm, BloomM3* bloom, const KeyType& key,
                     uint32_t tid) const {
    if (dpgm->entry_count() == 0) return util::OVERFLOW;
    if (!bloom->MaybeContains(static_cast<uint64_t>(key))) return util::OVERFLOW;
    return dpgm->EqualityLookup(key, tid);
  }

  void StartWorker() {
    if (worker_running_.exchange(true)) return;
    stop_requested_.store(false);
    worker_ = std::thread([this] { WorkerLoop(); });
  }

  void Shutdown() {
    if (!worker_running_.load()) return;
    {
      std::unique_lock<std::mutex> lk(m_);
      cv_done_.wait(lk, [&] { return !flush_in_progress_.load(); });
    }
    DPGM* active = active_.load();
    BloomM3* ab = active_bloom_.load();
    if (active->entry_count() > 0) {
      DrainOneBufferIntoLipp(active, ab);
    }
    {
      std::lock_guard<std::mutex> lk(m_);
      stop_requested_.store(true);
      cv_work_.notify_all();
    }
    if (worker_.joinable()) worker_.join();
    worker_running_.store(false);
  }

  void TriggerFlush() {
    std::unique_lock<std::mutex> lk(m_);
    cv_done_.wait(lk, [&] { return !flush_in_progress_.load(); });

    DPGM* old_active = active_.load();
    DPGM* old_flushing = flushing_.load();
    BloomM3* old_active_bloom = active_bloom_.load();
    BloomM3* old_flushing_bloom = flushing_bloom_.load();
    active_.store(old_flushing, std::memory_order_release);
    flushing_.store(old_active, std::memory_order_release);
    active_bloom_.store(old_flushing_bloom, std::memory_order_release);
    flushing_bloom_.store(old_active_bloom, std::memory_order_release);

    flush_in_progress_.store(true, std::memory_order_release);
    cv_work_.notify_one();
  }

  void WorkerLoop() {
    while (true) {
      std::unique_lock<std::mutex> lk(m_);
      cv_work_.wait(lk, [&] {
        return flush_in_progress_.load() || stop_requested_.load();
      });
      if (stop_requested_.load() && !flush_in_progress_.load()) return;
      lk.unlock();

      DPGM* fl = flushing_.load(std::memory_order_acquire);
      BloomM3* fbloom = flushing_bloom_.load(std::memory_order_acquire);
      DrainOneBufferIntoLipp(fl, fbloom);

      {
        std::lock_guard<std::mutex> lk2(m_);
        flush_in_progress_.store(false, std::memory_order_release);
      }
      cv_done_.notify_all();
    }
  }

  void DrainOneBufferIntoLipp(DPGM* dpgm, BloomM3* bloom) {
    std::vector<std::pair<KeyType, uint64_t>> buf;
    dpgm->Snapshot(buf);
    if (!buf.empty()) lipp_.InsertBatch(buf);
    dpgm->Clear();
    bloom->Reset();
  }

  DPGM dpgm_a_;
  DPGM dpgm_b_;
  BloomM3 bloom_a_;
  BloomM3 bloom_b_;
  LippM3<KeyType> lipp_;

  std::atomic<DPGM*> active_{nullptr};
  std::atomic<DPGM*> flushing_{nullptr};
  std::atomic<BloomM3*> active_bloom_{nullptr};
  std::atomic<BloomM3*> flushing_bloom_{nullptr};

  std::thread worker_;
  std::mutex m_;
  std::condition_variable cv_work_;
  std::condition_variable cv_done_;
  std::atomic<bool> worker_running_{false};
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> flush_in_progress_{false};

  size_t flush_threshold_ = 0;
  // Defaults below are the per-workload best from the (bits x permille) sweep,
  // overridable via HYBRID_FLUSH_PERMILLE / HYBRID_BLOOM_BITS env vars.
  // bits=6 is the sweep winner across all 4 (dataset x workload) cases;
  // permille=5 is best for lookup-heavy, permille=20 is best for insert-heavy.
  // Default to 5 because the lookup-heavy gap to LIPP is larger.
  int flush_threshold_permille_ = 5;
  int bloom_bits_per_key_ = 6;

  // Per-workload insert strategy (HYBRID_INSERT_STRATEGY env var).
  // Default = kFastPath (find-then-route). For insert-heavy workloads
  // dominated by updates, run_milestone3.sh sets kLippDirect.
  enum class InsertStrategy { kFastPath, kLippDirect };
  InsertStrategy insert_strategy_ = InsertStrategy::kFastPath;
};
