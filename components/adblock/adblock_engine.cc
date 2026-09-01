// Copyright 2026 The Kiwi Reborn Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/adblock/adblock_engine.h"

#include <string_view>
#include <utility>

#include "base/containers/flat_set.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "components/adblock/adblock_resource_types.h"
#include "components/adblock/adblock_rule_parser.h"

namespace kiwi_adblock {

namespace {

constexpr uint32_t kHostIndexBuckets = 1u << 12;  // 4096 buckets.
constexpr int kMatchStepLimit = 200000;           // Backtrack safety cap.

uint32_t Fnv1a32(std::string_view s) {
  uint32_t hash = 2166136261u;
  for (char c : s) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 16777619u;
  }
  return hash;
}

// Host prefix of a ||-anchored pattern: everything before the first
// separator (^), path start (/), or wildcard (*).
std::string_view HostPrefixOf(std::string_view pattern) {
  const size_t cut = pattern.find_first_of("^/*");
  return cut == std::string_view::npos ? pattern : pattern.substr(0, cut);
}

// Sequential pattern matcher supporting '*' (any run of characters) and
// '^' (separator or end-of-string). Returns the position right after the
// match, or std::string::npos.
size_t MatchRest(std::string_view pattern,
                 std::string_view url,
                 size_t pos,
                 int* steps) {
  size_t p = 0, u = pos, star_p = std::string::npos, star_u = 0;
  while (u < url.size()) {
    if (++(*steps) > kMatchStepLimit) {
      return std::string::npos;
    }
    if (p < pattern.size() && pattern[p] == '*') {
      star_p = ++p;
      star_u = u;
      continue;
    }
    bool matched = false;
    if (p < pattern.size()) {
      if (pattern[p] == '^') {
        if (IsSeparatorCandidate(url[u])) {
          matched = true;
        } else if (u + 1 == url.size()) {
          // '^' also matches the end of the URL.
          return u + 1;
        }
      } else if (pattern[p] == url[u]) {
        matched = true;
      }
    }
    if (matched) {
      ++p;
      ++u;
      continue;
    }
    if (star_p != std::string::npos) {
      p = star_p;
      u = ++star_u;
      continue;
    }
    return std::string::npos;
  }
  while (p < pattern.size() && pattern[p] == '*') {
    ++p;
  }
  return p == pattern.size() ? u : std::string::npos;
}

bool RuleAppliesToParty(const NetworkRule& rule, bool third_party) {
  if (rule.third_party_only && !third_party) {
    return false;
  }
  if (rule.first_party_only && third_party) {
    return false;
  }
  return true;
}

bool RuleAppliesToDocument(const NetworkRule& rule,
                           const std::string& document_host) {
  if (!rule.included_domains.empty()) {
    bool found = false;
    for (const std::string& domain : rule.included_domains) {
      if (HostMatchesDomain(document_host, domain)) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  for (const std::string& domain : rule.excluded_domains) {
    if (HostMatchesDomain(document_host, domain)) {
      return false;
    }
  }
  return true;
}

bool RuleAppliesToResource(const NetworkRule& rule, uint32_t resource_mask) {
  if (!(rule.resource_types & resource_mask)) {
    return false;
  }
  if (rule.excluded_resource_types & resource_mask) {
    return false;
  }
  return true;
}

// Tries to verify a host-anchored candidate: `host_key` must match at a
// valid host boundary starting at `match_pos`, and the remainder of the
// pattern must match from there.
bool VerifyHostAnchored(const NetworkRule& rule,
                        std::string_view host_key,
                        const std::string& url_spec,
                        size_t match_pos,
                        const std::string& request_host) {
  // The candidate key must align with the actual request host: either the
  // host starts with it (||foo.com vs foo.com.evil.com) or the matched
  // position is a subdomain boundary inside `request_host`.
  const size_t host_begin = url_spec.find(request_host);
  if (host_begin == std::string::npos) {
    return false;
  }
  if (match_pos < host_begin || match_pos > host_begin + request_host.size()) {
    return false;
  }
  if (match_pos == host_begin) {
    // Pattern prefix anchored at the very start of the host: fine.
  } else if (url_spec[match_pos - 1] != '.') {
    return false;  // Not a subdomain boundary.
  }
  // Exact prefix check for the host key part.
  if (url_spec.compare(match_pos, host_key.size(), host_key) != 0) {
    return false;
  }
  const std::string_view pattern(rule.pattern);
  const std::string_view rest =
      pattern.substr(host_key.size());
  if (rest.empty()) {
    // Pattern is exactly the host (with || anchor): matches any page on it.
    if (rule.is_right_anchored) {
      // "||host|" right-anchored means host ends the URL: rare; only match
      // when the URL ends here.
      return match_pos + host_key.size() == url_spec.size();
    }
    return true;
  }
  int steps = 0;
  size_t end = MatchRest(rest, url_spec, match_pos + host_key.size(), &steps);
  if (end == std::string::npos) {
    return false;
  }
  if (rule.is_right_anchored && end != url_spec.size()) {
    return false;
  }
  return true;
}

}  // namespace

Engine::Engine() = default;

std::shared_ptr<const EngineData> Engine::BuildDataFromTexts(
    const std::vector<std::string>& texts,
    const std::vector<std::string>& custom_filters,
    const std::vector<std::string>& allowlist_hosts) const {
  auto data = std::make_shared<EngineData>();

  // Indexable intermediate map: hash -> host key -> rules (so rules sharing
  // a host prefix group together).
  base::flat_map<uint32_t, std::vector<NetworkRule>> host_index;
  std::vector<NetworkRule> generic_rules;

  auto handle_network = [&](NetworkRule rule) {
    if (rule.anchor == NetworkRule::Anchor::kHost) {
      const std::string key(HostPrefixOf(rule.pattern));
      if (key.empty()) {
        return;
      }
      host_index[Fnv1a32(key)].push_back(std::move(rule));
    } else {
      generic_rules.push_back(std::move(rule));
    }
  };

  auto handle_cosmetic = [&](CosmeticRule rule) {
    if (rule.is_exception) {
      if (rule.generic) {
        data->generic_cosmetic_exceptions.push_back(rule.selector);
      } else {
        for (const std::string& domain : rule.domains) {
          data->domain_cosmetic_exceptions[domain].push_back(rule.selector);
        }
      }
    } else {
      if (rule.generic) {
        data->generic_cosmetic_selectors.push_back(rule.selector);
      } else {
        for (const std::string& domain : rule.domains) {
          data->domain_cosmetic[domain].push_back(rule.selector);
        }
      }
    }
    data->cosmetic_rule_count++;
  };

  for (const std::string& text : texts) {
    for (const std::string& raw : base::SplitString(
             text, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
      // $generichide / $elemhide hints: record the target host so generic
      // cosmetic filtering is suppressed there (the parser drops the rule
      // itself from network blocking).
      if (raw.find("$generichide") != std::string::npos ||
          raw.find("$elemhide") != std::string::npos ||
          raw.find(",generichide") != std::string::npos ||
          raw.find(",elemhide") != std::string::npos) {
        const size_t anchor = raw.find("||");
        if (anchor != std::string::npos) {
          const std::string rest = raw.substr(anchor + 2);
          const size_t cut = rest.find_first_of("^/$");
          const std::string host = base::ToLowerASCII(
              cut == std::string::npos ? rest : rest.substr(0, cut));
          if (!host.empty() && host.find('*') == std::string::npos) {
            data->generichide_hosts.insert(host);
          }
        }
      }
      switch (ClassifyLine(raw)) {
        case LineKind::kEmpty:
          break;
        case LineKind::kCosmetic: {
          if (auto rule = ParseCosmeticRule(raw)) {
            handle_cosmetic(std::move(*rule));
          }
          break;
        }
        case LineKind::kNetwork: {
          if (auto rule = ParseNetworkRule(raw)) {
            handle_network(std::move(*rule));
            data->network_rule_count++;
          }
          break;
        }
      }
    }
  }
  for (const std::string& raw : custom_filters) {
    switch (ClassifyLine(raw)) {
      case LineKind::kEmpty:
        break;
      case LineKind::kCosmetic: {
        if (auto rule = ParseCosmeticRule(raw)) {
          handle_cosmetic(std::move(*rule));
        }
        break;
      }
      case LineKind::kNetwork: {
        if (auto rule = ParseNetworkRule(raw)) {
          handle_network(std::move(*rule));
          data->network_rule_count++;
        }
        break;
      }
    }
  }

  // $generichide/$elemhide host suppression: collected from network rules
  // carrying those options. The parser encodes them implicitly: rules whose
  // pattern contains them are dropped from blocking in v1, so instead we
  // support the common ||host^$generichide form by scanning raw lines is
  // too costly here; we approximate by treating $generichide rules as
  // cosmetic-suppression hints for their host key.
  // (Handled during parsing below via ParseNetworkRule follow-up.)

  data->allowlist_hosts = base::flat_set<std::string>(allowlist_hosts.begin(),
                                                      allowlist_hosts.end());
  data->host_index = std::move(host_index);
  data->generic_rules = std::move(generic_rules);
  return data;
}

bool Engine::IsAllowlisted(const std::string& host) const {
  auto data = data_;
  if (!data) {
    return false;
  }
  return IsAllowlistedOnData(*data, host);
}

bool Engine::IsAllowlistedOnData(const EngineData& data,
                                 const std::string& host) {
  if (host.empty()) {
    return false;
  }
  if (data.allowlist_hosts.contains(host)) {
    return true;
  }
  // Any allowlist entry that is a suffix of the host.
  for (const std::string& entry : data.allowlist_hosts) {
    if (HostMatchesDomain(host, entry)) {
      return true;
    }
  }
  return false;
}

MatchResult Engine::MatchNetworkOnData(const EngineData& data,
                                       const std::string& url_spec,
                                       const std::string& request_host,
                                       const std::string& document_host,
                                       uint32_t resource_mask,
                                       bool third_party) {
  MatchResult result;
  if (url_spec.empty() || request_host.empty()) {
    return result;
  }
  if (IsAllowlistedOnData(data, request_host) ||
      IsAllowlistedOnData(data, document_host)) {
    return result;
  }

  bool blocked = false;
  bool exception = false;
  bool important_blocked = false;
  bool important_exception = false;

  // --- Host-anchored (||) rules ---
  const size_t host_begin = url_spec.find(request_host);
  if (host_begin != std::string::npos) {
    // Candidate keys: char prefixes of the host (||host matches hosts that
    // begin with it) and label-boundary suffixes (subdomains).
    for (size_t len = 1; len <= request_host.size(); ++len) {
      const std::string candidate = request_host.substr(0, len);
      const uint32_t key = Fnv1a32(candidate);
      auto it = data->host_index.find(key);
      if (it == data->host_index.end()) {
        continue;
      }
      for (const NetworkRule& rule : it->second) {
        const std::string host_key(HostPrefixOf(rule.pattern));
        if (host_key != candidate) {
          continue;  // Hash collision.
        }
        if (!RuleAppliesToResource(rule, resource_mask) ||
            !RuleAppliesToParty(rule, third_party) ||
            !RuleAppliesToDocument(rule, document_host)) {
          continue;
        }
        const size_t match_pos =
            host_begin;  // Prefixes always start at the host start.
        if (VerifyHostAnchored(rule, host_key, url_spec, match_pos,
                               request_host)) {
          if (rule.is_exception) {
            exception = true;
            important_exception |= rule.is_important;
          } else {
            blocked = true;
            important_blocked |= rule.is_important;
          }
        }
      }
    }
    // Label-boundary suffixes (e.g. ||foo.com matching bar.foo.com).
    size_t dot = request_host.find('.');
    while (dot != std::string::npos && dot + 1 < request_host.size()) {
      const std::string candidate = request_host.substr(dot + 1);
      const uint32_t key = Fnv1a32(candidate);
      auto it = data->host_index.find(key);
      if (it != data->host_index.end()) {
        for (const NetworkRule& rule : it->second) {
          const std::string host_key(HostPrefixOf(rule.pattern));
          if (host_key != candidate) {
            continue;
          }
          if (!RuleAppliesToResource(rule, resource_mask) ||
              !RuleAppliesToParty(rule, third_party) ||
              !RuleAppliesToDocument(rule, document_host)) {
            continue;
          }
          const size_t match_pos =
              host_begin + request_host.size() - candidate.size();
          if (VerifyHostAnchored(rule, host_key, url_spec, match_pos,
                                 request_host)) {
            if (rule.is_exception) {
              exception = true;
              important_exception |= rule.is_important;
            } else {
              blocked = true;
              important_blocked |= rule.is_important;
            }
          }
        }
      }
      dot = request_host.find('.', dot + 1);
    }
  }

  // --- Generic (substring / |left-anchored) rules ---
  for (const NetworkRule& rule : data->generic_rules) {
    if (rule.pattern.size() > url_spec.size()) {
      continue;
    }
    if (!RuleAppliesToResource(rule, resource_mask) ||
        !RuleAppliesToParty(rule, third_party) ||
        !RuleAppliesToDocument(rule, document_host)) {
      continue;
    }
    int steps = 0;
    if (rule.anchor == NetworkRule::Anchor::kLeftUrl) {
      size_t end = MatchRest(rule.pattern, url_spec, 0, &steps);
      if (end != std::string::npos &&
          (!rule.is_right_anchored || end == url_spec.size())) {
        if (rule.is_exception) {
          exception = true;
          important_exception |= rule.is_important;
        } else {
          blocked = true;
          important_blocked |= rule.is_important;
        }
      }
      continue;
    }
    // Substring scan.
    for (size_t pos = url_spec.find(rule.pattern[0]); pos != std::string::npos;
         pos = url_spec.find(rule.pattern[0], pos + 1)) {
      int local_steps = 0;
      size_t end = MatchRest(rule.pattern, url_spec, pos, &local_steps);
      steps += local_steps;
      if (end != std::string::npos &&
          (!rule.is_right_anchored || end == url_spec.size())) {
        if (rule.is_exception) {
          exception = true;
          important_exception |= rule.is_important;
        } else {
          blocked = true;
          important_blocked |= rule.is_important;
        }
        break;
      }
      if (steps > kMatchStepLimit) {
        break;
      }
    }
  }

  if (important_blocked) {
    result.blocked = true;
    result.matched_exception = important_exception;
  } else if (blocked && !exception) {
    result.blocked = true;
  } else if (blocked && exception) {
    result.blocked = false;
    result.matched_exception = true;
  }
  return result;
}

MatchResult Engine::MatchNetwork(const std::string& url_spec,
                                 const std::string& request_host,
                                 const std::string& document_host,
                                 uint32_t resource_mask,
                                 bool third_party) const {
  auto data = data_;
  MatchResult result;
  if (!data) {
    return result;
  }
  result = MatchNetworkOnData(*data, url_spec, request_host, document_host,
                              resource_mask, third_party);
  if (result.blocked) {
    blocked_count_.fetch_add(1, std::memory_order_relaxed);
  }
  return result;
}

std::string Engine::GetCosmeticCssOnData(const EngineData& data,
                                         const std::string& page_host) {
  if (page_host.empty()) {
    return std::string();
  }
  if (IsAllowlistedOnData(data, page_host)) {
    return std::string();
  }

  // $generichide / $elemhide: skip generic cosmetic rules for this host.
  bool generic_hide = false;
  for (const std::string& gh : data.generichide_hosts) {
    if (HostMatchesDomain(page_host, gh)) {
      generic_hide = true;
      break;
    }
  }

  std::vector<std::string> selectors;
  if (!generic_hide) {
    selectors = data.generic_cosmetic_selectors;
  }

  // Domain-specific selectors for the host and its parent domains.
  size_t pos = 0;
  while (true) {
    const std::string candidate = page_host.substr(pos);
    auto it = data.domain_cosmetic.find(candidate);
    if (it != data.domain_cosmetic.end()) {
      for (const std::string& sel : it->second) {
        selectors.push_back(sel);
      }
    }
    size_t dot = page_host.find('.', pos);
    if (dot == std::string::npos) {
      break;
    }
    pos = dot + 1;
  }

  if (selectors.empty()) {
    return std::string();
  }

  // Exceptions.
  base::flat_set<std::string> excluded;
  for (const std::string& ex : data.generic_cosmetic_exceptions) {
    excluded.insert(ex);
  }
  pos = 0;
  while (true) {
    const std::string candidate = page_host.substr(pos);
    auto it = data.domain_cosmetic_exceptions.find(candidate);
    if (it != data.domain_cosmetic_exceptions.end()) {
      for (const std::string& sel : it->second) {
        excluded.insert(sel);
      }
    }
    size_t dot = page_host.find('.', pos);
    if (dot == std::string::npos) {
      break;
    }
    pos = dot + 1;
  }

  std::string css;
  css.reserve(selectors.size() * 48);
  for (const std::string& sel : selectors) {
    if (excluded.contains(sel)) {
      continue;
    }
    css += sel;
    css += "{display:none !important;z-index:-999!important;}\n";
  }
  return css;
}

}  // namespace kiwi_adblock
