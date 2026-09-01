// Copyright 2026 The Kiwi Reborn Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/android/chrome_jni_headers/KiwiFlagsBridge_jni.h"

#include "base/android/jni_android.h"
#include "chrome/browser/kiwi/kiwi_flags_service.h"

static void JNI_KiwiFlagsBridge_Natives_SetBackgroundPlaybackEnabled(
    JNIEnv* env,
    jboolean enabled) {
  KiwiFlagsService::GetInstance()->SetBackgroundPlaybackEnabled(enabled);
}
