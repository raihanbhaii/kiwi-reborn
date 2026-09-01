// Copyright 2026 The Kiwi Reborn Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/adblock/adblock_service.h"

#include "base/functional/bind.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "components/adblock/adblock_engine.h"

namespace {

// Rebuild must not run on the UI thread: parsing large lists is CPU-bound.
void RebuildOnWorkerThread(
    std::vector<std::string> texts,
    std::vector<std::string> custom_filters,
    std::vector<std::string> allowlist_hosts) {
  auto data = kiwi_adblock::Engine().BuildDataFromTexts(
      texts, custom_filters, allowlist_hosts);
  AdblockService::GetInstance()->CommitBuiltData(std::move(data));
}

std::vector<std::string> ParseHostList(const std::string& text) {
  std::vector<std::string> hosts;
  for (const std::string& line : base::SplitString(
           text, "\n,", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    std::string host = base::ToLowerASCII(line);
    // Strip a scheme if the user pasted a URL.
    const size_t scheme_end = host.find("://");
    if (scheme_end != std::string::npos) {
      host = host.substr(scheme_end + 3);
    }
    const size_t slash = host.find_first_of("/?#");
    if (slash != std::string::npos) {
      host = host.substr(0, slash);
    }
    if (!host.empty() && host.find('*') == std::string::npos) {
      hosts.push_back(host);
    }
  }
  return hosts;
}

}  // namespace

AdblockService* AdblockService::GetInstance() {
  static base::NoDestructor<AdblockService> instance;
  return instance.get();
}

AdblockService::AdblockService() = default;

void AdblockService::SetEnabled(bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  enabled_ = enabled;
}

bool AdblockService::IsEnabled() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return enabled_;
}

void AdblockService::SetListContent(int list_id, std::string content) {
  std::lock_guard<std::mutex> lock(mutex_);
  pending_lists_[list_id] = std::move(content);
}

void AdblockService::SetCustomFilters(std::string custom_filters) {
  std::lock_guard<std::mutex> lock(mutex_);
  custom_filters_ = std::move(custom_filters);
}

void AdblockService::SetAllowlistContent(std::string allowlist_text) {
  std::lock_guard<std::mutex> lock(mutex_);
  allowlist_text_ = std::move(allowlist_text);
}

void AdblockService::Rebuild() {
  std::vector<std::string> texts;
  std::string custom;
  std::string allowlist;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, content] : pending_lists_) {
      texts.push_back(content);
    }
    custom = custom_filters_;
    allowlist = allowlist_text_;
  }
  base::ThreadPool::PostSequencedTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&RebuildOnWorkerThread, std::move(texts),
                     std::vector<std::string>{std::move(custom)},
                     ParseHostList(allowlist)));
}

void AdblockService::CommitBuiltData(
    std::shared_ptr<const kiwi_adblock::EngineData> data) {
  std::lock_guard<std::mutex> lock(mutex_);
  data_ = std::move(data);
}

kiwi_adblock::MatchResult AdblockService::MatchNetwork(
    const std::string& url_spec,
    const std::string& request_host,
    const std::string& document_host,
    uint32_t resource_mask,
    bool third_party) {
  std::shared_ptr<const kiwi_adblock::EngineData> snapshot;
  bool enabled;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled = enabled_;
    snapshot = data_;
  }
  kiwi_adblock::MatchResult result;
  if (!enabled || !snapshot) {
    return result;
  }
  result = kiwi_adblock::Engine::MatchNetworkOnData(
      *snapshot, url_spec, request_host, document_host, resource_mask,
      third_party);
  if (result.blocked) {
    blocked_count_.fetch_add(1, std::memory_order_relaxed);
  }
  return result;
}

std::string AdblockService::GetCosmeticCss(const std::string& page_host) {
  std::shared_ptr<const kiwi_adblock::EngineData> snapshot;
  bool enabled;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled = enabled_;
    snapshot = data_;
  }
  if (!enabled || !snapshot) {
    return std::string();
  }
  return kiwi_adblock::Engine::GetCosmeticCssOnData(*snapshot, page_host);
}

size_t AdblockService::network_rule_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return data_ ? data_->network_rule_count : 0;
}

size_t AdblockService::cosmetic_rule_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return data_ ? data_->cosmetic_rule_count : 0;
}
