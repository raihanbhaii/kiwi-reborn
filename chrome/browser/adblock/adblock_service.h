// Copyright 2026 The Kiwi Reborn Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ADBLOCK_ADBLOCK_SERVICE_H_
#define CHROME_BROWSER_ADBLOCK_ADBLOCK_SERVICE_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/no_destructor.h"
#include "components/adblock/adblock_engine.h"

// Process-wide adblock state. All methods are safe to call from any thread;
// the rule snapshot is handed out as an immutable shared_ptr so request
// matching never blocks filter-list updates.
class AdblockService {
 public:
  static AdblockService* GetInstance();

  AdblockService(const AdblockService&) = delete;
  AdblockService& operator=(const AdblockService&) = delete;

  void SetEnabled(bool enabled);
  bool IsEnabled() const;

  // Stores one filter list's raw text (called per list, then Rebuild()).
  void SetListContent(int list_id, std::string content);
  void SetCustomFilters(std::string custom_filters);
  void SetAllowlistContent(std::string allowlist_text);
  // Rebuilds the engine snapshot from the stored lists (call on a worker
  // thread; list parsing takes tens of milliseconds per MB).
  void Rebuild();
  // Installs a freshly built snapshot (internal; called by the worker).
  void CommitBuiltData(std::shared_ptr<const kiwi_adblock::EngineData> data);

  // Network-level decision (called from URLLoaderThrottle on IO sequences).
  kiwi_adblock::MatchResult MatchNetwork(
      const std::string& url_spec,
      const std::string& request_host,
      const std::string& document_host,
      uint32_t resource_mask,
      bool third_party);

  // Cosmetic CSS for a page host (called from the UI thread tab helper).
  std::string GetCosmeticCss(const std::string& page_host);

  uint64_t blocked_count() const {
    return blocked_count_.load(std::memory_order_relaxed);
  }
  void ResetBlockedCount() { blocked_count_.store(0); }

  size_t network_rule_count() const;
  size_t cosmetic_rule_count() const;

 private:
  friend class base::NoDestructor<AdblockService>;
  AdblockService();
  ~AdblockService() = default;

  mutable std::mutex mutex_;
  bool enabled_ = true;
  std::shared_ptr<const kiwi_adblock::EngineData> data_;
  std::unordered_map<int, std::string> pending_lists_;
  std::string custom_filters_;
  std::string allowlist_text_;

  std::atomic<uint64_t> blocked_count_{0};
};

#endif  // CHROME_BROWSER_ADBLOCK_ADBLOCK_SERVICE_H_
