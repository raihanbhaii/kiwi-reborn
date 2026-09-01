// Copyright 2026 The Kiwi Reborn Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_ADBLOCK_ADBLOCK_ENGINE_H_
#define COMPONENTS_ADBLOCK_ADBLOCK_ENGINE_H_

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "components/adblock/adblock_filter.h"

namespace kiwi_adblock {

// One immutable snapshot of the full rule set. Built off-thread by
// Engine::ReplaceRulesFromTexts and swapped in atomically by the owning
// service. Readers therefore never block writers and vice versa.
struct EngineData {
  // ||host-anchored rules, indexed by FNV-1a32 hash of the rule's host
  // prefix (the part before the first '^', '/' or '*').
  base::flat_map<uint32_t, std::vector<NetworkRule>> host_index;
  // Plain-substring / left-anchored rules, scanned linearly.
  std::vector<NetworkRule> generic_rules;

  // Cosmetic rules.
  std::vector<std::string> generic_cosmetic_selectors;
  std::vector<std::string> generic_cosmetic_exceptions;
  // Exact-domain cosmetic rules: domain -> selectors / exceptions.
  base::flat_map<std::string, std::vector<std::string>> domain_cosmetic;
  base::flat_map<std::string, std::vector<std::string>> domain_cosmetic_exceptions;

  // Hosts with $generichide / $elemhide document rules: generic cosmetic
  // filtering is suppressed there.
  base::flat_set<std::string> generichide_hosts;

  // UI-driven per-site allowlist (host suffixes).
  base::flat_set<std::string> allowlist_hosts;

  size_t network_rule_count = 0;
  size_t cosmetic_rule_count = 0;
};

// The matching engine. Thread-safe under the owning service's lock; the
// heavy work (rule application) happens on immutable EngineData snapshots.
class Engine {
 public:
  Engine();

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // Builds a fresh snapshot from raw filter-list texts (EasyList / ABP
  // syntax). `custom_filters` are appended last. Returns the new snapshot;
  // the caller hands it to CommitData().
  std::shared_ptr<const EngineData> BuildDataFromTexts(
      const std::vector<std::string>& texts,
      const std::vector<std::string>& custom_filters,
      const std::vector<std::string>& allowlist_hosts) const;

  // Swaps the active snapshot (engine + service keep it simple: one lock).
  void CommitData(std::shared_ptr<const EngineData> data) {
    data_ = std::move(data);
  }

  std::shared_ptr<const EngineData> data() const { return data_; }

  // Evaluates one network request. `url_spec` must be the lowercased URL
  // spec, `request_host` the URL's host, `document_host` the host of the
  // top-level document. `resource_mask` is a bitmask of ResourceType values
  // (all bits that apply to the request set). Returns whether the request
  // must be blocked.
  MatchResult MatchNetwork(const std::string& url_spec,
                           const std::string& request_host,
                           const std::string& document_host,
                           uint32_t resource_mask,
                           bool third_party) const;

  // Lock-free variant used by the browser service: callers grab a snapshot
  // of EngineData under a short lock, then match without holding it. Does
  // not touch the blocked counter.
  static MatchResult MatchNetworkOnData(const EngineData& data,
                                        const std::string& url_spec,
                                        const std::string& request_host,
                                        const std::string& document_host,
                                        uint32_t resource_mask,
                                        bool third_party);

  // Lock-free cosmetic CSS generation on a snapshot.
  static std::string GetCosmeticCssOnData(const EngineData& data,
                                          const std::string& page_host);

  // Lock-free allowlist check on a snapshot.
  static bool IsAllowlistedOnData(const EngineData& data,
                                  const std::string& host);

  size_t network_rule_count() const {
    auto data = data_;
    return data ? data->network_rule_count : 0;
  }
  size_t cosmetic_rule_count() const {
    auto data = data_;
    return data ? data->cosmetic_rule_count : 0;
  }

  // Blocked-request counter (monotonic; UI polls it).
  uint64_t blocked_count() const {
    return blocked_count_.load(std::memory_order_relaxed);
  }
  void ResetBlockedCount() { blocked_count_.store(0); }

 private:
  bool IsAllowlisted(const std::string& host) const;

  std::shared_ptr<const EngineData> data_;
  std::atomic<uint64_t> blocked_count_{0};
};

}  // namespace kiwi_adblock

#endif  // COMPONENTS_ADBLOCK_ADBLOCK_ENGINE_H_
