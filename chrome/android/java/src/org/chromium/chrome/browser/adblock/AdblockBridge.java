// Copyright 2026 The Kiwi Reborn Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.adblock;

import org.chromium.base.annotations.NativeMethods;

/**
 * Java ↔ native bridge for the Kiwi Reborn native adblock engine.
 * The native counterpart lives in chrome/browser/adblock/adblock_bridge.cc.
 */
public class AdblockBridge {
    private AdblockBridge() {}

    @NativeMethods
    interface Natives {
        void setEnabled(boolean enabled);
        void setListContent(int listId, String content);
        void setCustomFilters(String filters);
        void setAllowlistContent(String allowlist);
        void rebuild();
        long getBlockedCount();
        void resetBlockedCount();
        long getNetworkRuleCount();
        long getCosmeticRuleCount();
    }
}
