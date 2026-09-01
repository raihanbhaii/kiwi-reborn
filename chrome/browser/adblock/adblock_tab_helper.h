// Copyright 2026 The Kiwi Reborn Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ADBLOCK_ADBLOCK_TAB_HELPER_H_
#define CHROME_BROWSER_ADBLOCK_ADBLOCK_TAB_HELPER_H_

#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content {
class RenderFrameHost;
class WebContents;
}  // namespace content

// Injects cosmetic-filter CSS into documents as they finish loading. The CSS
// comes from the engine snapshot in AdblockService and is applied in an
// isolated world so page scripts cannot see or fight it.
class AdblockTabHelper : public content::WebContentsObserver,
                         public content::WebContentsUserData<AdblockTabHelper> {
 public:
  ~AdblockTabHelper() override;

  // Isolated world id used for cosmetic injection. Chrome's own ids occupy
  // CONTENT_END+1..+6 (translate, indigo, internal, applescript,
  // extensions); we take a free slot in the reserved range.
  static constexpr int32_t kCosmeticWorldId =
      content::ISOLATED_WORLD_ID_CONTENT_END + 8;

  void DidFinishLoad(content::RenderFrameHost* render_frame_host) override;

  WEB_CONTENTS_USER_DATA_KEY_DECL();

 private:
  explicit AdblockTabHelper(content::WebContents* web_contents);
  friend class content::WebContentsUserData<AdblockTabHelper>;
};

#endif  // CHROME_BROWSER_ADBLOCK_ADBLOCK_TAB_HELPER_H_
