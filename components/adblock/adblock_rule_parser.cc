// Copyright 2026 The Kiwi Reborn Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/adblock/adblock_rule_parser.h"

#include <string_view>

#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "components/adblock/adblock_resource_types.h"

namespace kiwi_adblock {

namespace {

bool IsSeparatorCandidate(char c) {
  // ABP separator (^): any character that is not a letter, digit, or one of
  // the unreserved characters _ - . %.
  if (base::IsAsciiAlpha(c) || base::IsAsciiDigit(c)) {
    return false;
  }
  switch (c) {
    case '_':
    case '-':
    case '.':
    case '%':
      return false;
    default:
      return true;
  }
}

uint32_t ResourceTypeFromOption(const std::string& option,
                                bool negated,
                                uint32_t& include_mask,
                                uint32_t& exclude_mask) {
  uint32_t bit;
  if (option == "script") {
    bit = kResourceScript;
  } else if (option == "image") {
    bit = kResourceImage;
  } else if (option == "stylesheet" || option == "style") {
    bit = kResourceStylesheet;
  } else if (option == "subdocument" || option == "frame" ||
             option == "iframe") {
    bit = kResourceSubdocument;
  } else if (option == "object" || option == "object-subrequest") {
    bit = kResourceObject;
  } else if (option == "xmlhttprequest" || option == "xhr") {
    bit = kResourceXmlHttpRequest;
  } else if (option == "font") {
    bit = kResourceFont;
  } else if (option == "media") {
    bit = kResourceMedia;
  } else if (option == "websocket") {
    bit = kResourceWebSocket;
  } else if (option == "other" || option == "webrtc" ||
             option == "worker") {
    bit = kResourceOther;
  } else if (option == "ping" || option == "beacon") {
    bit = kResourcePing;
  } else if (option == "document" || option == "popup" ||
             option == "main_frame") {
    bit = kResourceDocument;
  } else {
    return 0;  // Unknown / unsupported option.
  }
  if (negated) {
    exclude_mask |= bit;
    include_mask &= ~bit;
  } else {
    include_mask |= bit;
    exclude_mask &= ~bit;
  }
  return bit;
}

// Splits the `$options` suffix off the pattern. Returns false when the text
// after the last '$' does not look like a valid option list (so the '$' is
// part of the pattern).
bool SplitOptions(const std::string& input,
                  std::string* pattern_out,
                  std::string* options_out) {
  size_t dollar = input.rfind('$');
  if (dollar == std::string::npos || dollar == 0) {
    return false;
  }
  std::string options = input.substr(dollar + 1);
  if (options.empty()) {
    return false;
  }
  // Options may only contain option-ish characters.
  for (char c : options) {
    if (!base::IsAsciiAlpha(c) && c != '-' && c != '~' && c != ',' &&
        c != '=' && c != '|' && c != '.') {
      return false;
    }
  }
  // A pattern ending right before options with '?' (query) is still sane.
  *pattern_out = input.substr(0, dollar);
  *options_out = options;
  return true;
}

}  // namespace

bool HostMatchesDomain(const std::string& host, const std::string& domain) {
  if (host.empty() || domain.empty()) {
    return false;
  }
  if (host == domain) {
    return true;
  }
  return host.size() > domain.size() &&
         host.compare(host.size() - domain.size(), domain.size(), domain) ==
             0 &&
         host[host.size() - domain.size() - 1] == '.';
}

LineKind ClassifyLine(const std::string& line_raw) {
  std::string line;
  base::TrimWhitespaceASCII(line_raw, base::TRIM_ALL, &line);
  if (line.empty()) {
    return LineKind::kEmpty;
  }
  // Comments and list metadata.
  if (line[0] == '!' || line[0] == '[' || line[0] == '#') {
    // '#'-starting lines that are not cosmetic rules are treated as comments
    // (some lists use them for metadata).
    return LineKind::kEmpty;
  }
  // Scriptlet / procedural syntax we deliberately do not support yet.
  if (line.find("#$#") != std::string::npos ||
      line.find("#%#") != std::string::npos ||
      line.find("#@%#") != std::string::npos ||
      line.find("#@$#") != std::string::npos ||
      line.find("#$?") != std::string::npos ||
      line.find("#@?") != std::string::npos ||
      line.find("#?#") != std::string::npos) {
    return LineKind::kEmpty;
  }
  if (line.find("##") != std::string::npos ||
      line.find("#@#") != std::string::npos) {
    return LineKind::kCosmetic;
  }
  return LineKind::kNetwork;
}

std::optional<CosmeticRule> ParseCosmeticRule(const std::string& line_raw) {
  std::string line;
  base::TrimWhitespaceASCII(line_raw, base::TRIM_ALL, &line);
  const size_t marker = line.find("#@#");
  const bool is_exception = marker != std::string::npos;
  const size_t sep = is_exception ? marker : line.find("##");
  if (sep == std::string::npos) {
    return std::nullopt;
  }
  const std::string domain_part = line.substr(0, sep);
  std::string selector = line.substr(sep + (is_exception ? 3 : 2));
  base::TrimWhitespaceASCII(selector, base::TRIM_ALL, &selector);
  if (selector.empty() || selector.size() > 2048) {
    return std::nullopt;
  }

  CosmeticRule rule;
  rule.is_exception = is_exception;
  rule.selector = selector;
  if (domain_part.empty()) {
    rule.generic = true;
    return rule;
  }
  rule.generic = false;
  for (const std::string& raw :
       base::SplitString(domain_part, ",", base::TRIM_WHITESPACE,
                         base::SPLIT_WANT_NONEMPTY)) {
    std::string domain = base::ToLowerASCII(raw);
    if (base::StartsWith(domain, "~", base::CompareCase::INSENSITIVE_ASCII)) {
      domain = domain.substr(1);
      if (!domain.empty()) {
        rule.excluded_domains.push_back(domain);
      }
    } else if (!domain.empty()) {
      rule.domains.push_back(domain);
    }
  }
  if (rule.domains.empty() && rule.excluded_domains.empty()) {
    rule.generic = true;
  }
  return rule;
}

std::optional<NetworkRule> ParseNetworkRule(const std::string& line_raw) {
  std::string line;
  base::TrimWhitespaceASCII(line_raw, base::TRIM_ALL, &line);
  if (line.empty()) {
    return std::nullopt;
  }

  NetworkRule rule;
  if (base::StartsWith(line, "@@", base::CompareCase::INSENSITIVE_ASCII)) {
    rule.is_exception = true;
    line = line.substr(2);
  }
  if (line.empty()) {
    return std::nullopt;
  }

  // Options.
  std::string pattern;
  std::string options;
  if (SplitOptions(line, &pattern, &options)) {
    uint32_t include_mask = 0;
    uint32_t exclude_mask = 0;
    bool saw_any_type_option = false;
    for (const std::string& raw_opt : base::SplitString(
             options, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
      std::string opt = base::ToLowerASCII(raw_opt);
      bool negated =
          base::StartsWith(opt, "~", base::CompareCase::INSENSITIVE_ASCII);
      if (negated) {
        opt = opt.substr(1);
      }
      if (opt == "third-party") {
        rule.third_party_only = !negated;
        rule.first_party_only = negated;
      } else if (opt == "first-party") {
        rule.first_party_only = !negated;
        rule.third_party_only = negated;
      } else if (opt == "domain=" ||
                 base::StartsWith(opt, "domain=",
                                  base::CompareCase::INSENSITIVE_ASCII) ||
                 base::StartsWith(opt, "from=",
                                  base::CompareCase::INSENSITIVE_ASCII) ||
                 base::StartsWith(opt, "domains=",
                                  base::CompareCase::INSENSITIVE_ASCII)) {
        std::string domains_value = opt.substr(opt.find('=') + 1);
        rule.domain_restriction = true;
        for (const std::string& raw_dom : base::SplitString(
                 domains_value, "|", base::TRIM_WHITESPACE,
                 base::SPLIT_WANT_NONEMPTY)) {
          std::string dom = base::ToLowerASCII(raw_dom);
          if (base::StartsWith(dom, "~", base::CompareCase::INSENSITIVE_ASCII)) {
            dom = dom.substr(1);
            if (!dom.empty()) {
              rule.excluded_domains.push_back(dom);
            }
          } else if (!dom.empty()) {
            rule.included_domains.push_back(dom);
          }
        }
        if (rule.included_domains.empty()) {
          // A $domain= list with only exclusions still works: it applies
          // everywhere except the excluded hosts.
          rule.domain_restriction = false;
        }
      } else if (opt == "important") {
        if (negated) {
          return std::nullopt;
        }
        rule.is_important = true;
      } else if (opt == "match-case") {
        // We always match case-insensitively on lowercased input; harmless.
      } else if (opt == "popup") {
        // Popups are cancelled at main-frame document level.
        if (negated) {
          return std::nullopt;
        }
        include_mask |= kResourceDocument;
        saw_any_type_option = true;
      } else if (opt == "generichide" || opt == "elemhide" ||
                 opt == "genericblock" || opt == "csp" ||
                 base::StartsWith(opt, "csp=", base::CompareCase::ASCII)) {
        // Cosmetic-hint and CSP options cannot be honored as *network*
        // rules; the engine handles $generichide/$elemhide hosts separately
        // from the raw line, so drop the rule here to avoid over-blocking.
        return std::nullopt;
      } else if (opt == "redirect" ||
                 base::StartsWith(opt, "redirect=",
                                  base::CompareCase::ASCII)) {
        // Redirect-to-empty rules are approximated as plain blocks for the
        // resource types the rule lists, which is the safe subset of their
        // intended behavior.
      } else {
        uint32_t bit = ResourceTypeFromOption(opt, negated, include_mask,
                                              exclude_mask);
        if (bit == 0 && !negated) {
          // Unknown option: do not apply an over-broad rule.
          return std::nullopt;
        }
        if (bit != 0) {
          saw_any_type_option = true;
        }
      }
    }
    if (include_mask != 0 || exclude_mask != 0) {
      rule.resource_types = include_mask;
      if (rule.resource_types == 0) {
        rule.resource_types = kResourceAll;
      }
      rule.excluded_resource_types = exclude_mask;
    }
    (void)saw_any_type_option;
    line = pattern;
  }

  if (line.empty()) {
    return std::nullopt;
  }

  // Anchors.
  if (base::StartsWith(line, "||", base::CompareCase::ASCII)) {
    rule.anchor = NetworkRule::Anchor::kHost;
    line = line.substr(2);
  } else if (base::StartsWith(line, "|", base::CompareCase::ASCII)) {
    rule.anchor = NetworkRule::Anchor::kLeftUrl;
    line = line.substr(1);
  }
  if (!line.empty() && line.back() == '|') {
    rule.is_right_anchored = true;
    line.pop_back();
  }

  // Trailing '*' is meaningless.
  while (!line.empty() && line.back() == '*') {
    line.pop_back();
    rule.is_right_anchored = false;
  }
  if (line.empty()) {
    return std::nullopt;
  }

  rule.pattern = base::ToLowerASCII(line);
  if (rule.pattern.size() > 2048) {
    return std::nullopt;
  }
  return rule;
}

}  // namespace kiwi_adblock
