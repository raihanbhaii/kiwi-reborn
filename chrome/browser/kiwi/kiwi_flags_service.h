// Copyright 2026 The Kiwi Reborn Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_KIWI_KIWI_FLAGS_SERVICE_H_
#define CHROME_BROWSER_KIWI_KIWI_FLAGS_SERVICE_H_

#include <atomic>

// Process-wide Kiwi Reborn feature flags. Java pushes the persisted user
// preferences here at startup (and on change); browser-side code that spawns
// renderer processes consults these flags so renderer-side behavior (e.g.
// visibility spoofing for background playback) can follow the toggle.
class KiwiFlagsService {
 public:
  static KiwiFlagsService* GetInstance();

  KiwiFlagsService(const KiwiFlagsService&) = delete;
  KiwiFlagsService& operator=(const KiwiFlagsService&) = delete;

  void SetBackgroundPlaybackEnabled(bool enabled) {
    background_playback_enabled_.store(enabled, std::memory_order_relaxed);
  }
  bool IsBackgroundPlaybackEnabled() const {
    return background_playback_enabled_.load(std::memory_order_relaxed);
  }

 private:
  friend class base::NoDestructor<KiwiFlagsService>;
  KiwiFlagsService() = default;
  ~KiwiFlagsService() = default;

  std::atomic<bool> background_playback_enabled_{true};
};

#endif  // CHROME_BROWSER_KIWI_KIWI_FLAGS_SERVICE_H_
