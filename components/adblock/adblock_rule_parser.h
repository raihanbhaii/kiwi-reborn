// Copyright 2026 The Kiwi Reborn Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_ADBLOCK_ADBLOCK_RULE_PARSER_H_
#define COMPONENTS_ADBLOCK_ADBLOCK_RULE_PARSER_H_

#include <optional>
#include <string>

#include "components/adblock/adblock_filter.h"

namespace kiwi_adblock {

// Parses a single filter line (EasyList/ABP syntax subset) into either a
// network rule, a cosmetic rule, or nothing (comments, unsupported syntax).
// Returns std::nullopt when the line does not yield a usable rule.
std::optional<NetworkRule> ParseNetworkRule(const std::string& line_raw);
std::optional<CosmeticRule> ParseCosmeticRule(const std::string& line_raw);

enum class LineKind {
  kEmpty,
  kCosmetic,
  kNetwork,
};

// Classifies a raw filter line.
LineKind ClassifyLine(const std::string& line_raw);

// True when `host` equals `domain` or is a subdomain of it
// ("www.example.com" matches "example.com").
bool HostMatchesDomain(const std::string& host, const std::string& domain);

}  // namespace kiwi_adblock

#endif  // COMPONENTS_ADBLOCK_ADBLOCK_RULE_PARSER_H_
