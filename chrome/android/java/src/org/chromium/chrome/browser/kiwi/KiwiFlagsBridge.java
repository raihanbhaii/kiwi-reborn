// Copyright 2026 The Kiwi Reborn Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.kiwi;

import org.chromium.base.annotations.NativeMethods;

/**
 * Java ↔ native bridge for Kiwi Reborn Ultimate feature flags.
 * Native side: chrome/browser/kiwi/kiwi_flags_bridge.cc
 */
public class KiwiFlagsBridge {
    private KiwiFlagsBridge() {}

    @NativeMethods
    interface Natives {
        void setBackgroundPlaybackEnabled(boolean enabled);
    }
}
