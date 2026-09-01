// Copyright 2026 The Kiwi Reborn Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_ADBLOCK_ADBLOCK_FILTER_H_
#define COMPONENTS_ADBLOCK_ADBLOCK_FILTER_H_

#include <stdint.h>

#include <string>
#include <vector>

namespace kiwi_adblock {

// A parsed network filter rule ("||example.com/script.js$script,domain=foo.com").
struct NetworkRule {
  enum class Anchor : uint8_t {
    kNone = 0,        // plain substring match
    kLeftUrl = 1,     // |http...  anchored at the start of the URL
    kHost = 2,        // ||host^   anchored at a host boundary
    kRight = 3,       // ...jpg|   anchored at the end of the URL
  };

  // Normalized, lowercased pattern. `*` wildcards and `^` separators are kept
  // as-is; matching interprets them.
  std::string pattern;
  Anchor anchor = Anchor::kNone;
  bool is_right_anchored = false;
  bool is_exception = false;  // @@...
  bool is_important = false;  // $important wins over exceptions
  bool third_party_only = false;
  bool first_party_only = false;
  uint32_t resource_types = kResourceAll;      // $script etc.
  uint32_t excluded_resource_types = 0;        // $~script etc.
  std::vector<std::string> included_domains;   // $domain=a.com|b.com
  std::vector<std::string> excluded_domains;   // $domain=~blocked-nested
  bool domain_restriction = false;             // rule only applies on included_domains
};

// A parsed cosmetic rule ("example.com##.ad-banner" / "##.promo" / "#@#...").
struct CosmeticRule {
  std::string selector;                 // CSS selector, lowercased by parser not required
  bool is_exception = false;            // #@#
  bool generic = true;                  // applies to every site (no leading domains)
  std::vector<std::string> domains;     // scope when !generic
  std::vector<std::string> excluded_domains;  // ~domain scope
};

// Result of evaluating a request against the rule set.
struct MatchResult {
  bool blocked = false;
  bool matched_exception = false;
};

}  // namespace kiwi_adblock

#endif  // COMPONENTS_ADBLOCK_ADBLOCK_FILTER_H_
