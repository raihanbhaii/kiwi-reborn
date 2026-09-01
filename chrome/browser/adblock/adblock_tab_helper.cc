// Copyright 2026 The Kiwi Reborn Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/adblock/adblock_tab_helper.h"

#include <utility>

#include "base/json/string_escape.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/adblock/adblock_service.h"
#include "components/adblock/adblock_engine.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "url/gurl.h"

namespace {

constexpr char kCosmeticInjectScriptTemplate[] =
    "(function(css) {"
    "if (!css) return;"
    "try {"
    "  var style = document.createElement('style');"
    "  style.setAttribute('data-kiwi-adblock', '1');"
    "  style.textContent = css;"
    "  (document.head || document.documentElement).appendChild(style);"
    "  var pending = [];"
    "  new MutationObserver(function(muts) {"
    "    for (var i = 0; i < muts.length; i++) {"
    "      var nodes = muts[i].addedNodes;"
    "      for (var j = 0; j < nodes.length; j++) {"
    "        if (nodes[j].nodeType === 1) pending.push(nodes[j]);"
    "      }"
    "    }"
    "    if (pending.length) {"
    "      requestAnimationFrame(function() { pending.length = 0; });"
    "    }"
    "  }).observe(document.documentElement, {childList: true, subtree: true});"
    "} catch (e) {}"
    "})(%s);";

// Only these schemes carry web content worth hiding elements on.
bool IsInjectableScheme(const GURL& url) {
  return url.SchemeIsHTTPOrHTTPS() ||
         (url.SchemeIs("about") && !url.spec().empty());
}

}  // namespace

AdblockTabHelper::AdblockTabHelper(content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents) {}

AdblockTabHelper::~AdblockTabHelper() = default;

void AdblockTabHelper::DidFinishLoad(
    content::RenderFrameHost* render_frame_host) {
  if (!render_frame_host) {
    return;
  }
  const GURL url = render_frame_host->GetLastCommittedURL();
  if (!IsInjectableScheme(url) || url.host().empty()) {
    return;
  }

  const std::string page_host = url.host();
  const std::string css =
      AdblockService::GetInstance()->GetCosmeticCss(page_host);
  if (css.empty()) {
    return;
  }

  std::string escaped_css;
  base::EscapeJSONString(css, false, &escaped_css);
  const std::string script =
      base::StringPrintf(kCosmeticInjectScriptTemplate, escaped_css.c_str());

  render_frame_host->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(script),
      /*callback=*/base::DoNothing::OnceCallback<void(base::Value)>(),
      AdblockTabHelper::kCosmeticWorldId);
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(AdblockTabHelper);
