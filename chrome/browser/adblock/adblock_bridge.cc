// Copyright 2026 The Kiwi Reborn Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/android/chrome_jni_headers/AdblockBridge_jni.h"

#include <string>

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "chrome/browser/adblock/adblock_service.h"

using base::android::ConvertJavaStringToUTF8;
using base::android::JavaParamRef;

// Java: org.chromium.chrome.browser.adblock.AdblockBridge.Natives
static void JNI_AdblockBridge_Natives_SetEnabled(JNIEnv* env, jboolean enabled) {
  AdblockService::GetInstance()->SetEnabled(enabled);
}

static void JNI_AdblockBridge_Natives_SetListContent(
    JNIEnv* env,
    jint list_id,
    const JavaParamRef<jstring>& content) {
  AdblockService::GetInstance()->SetListContent(
      list_id, ConvertJavaStringToUTF8(env, content));
}

static void JNI_AdblockBridge_Natives_SetCustomFilters(
    JNIEnv* env,
    const JavaParamRef<jstring>& filters) {
  AdblockService::GetInstance()->SetCustomFilters(
      ConvertJavaStringToUTF8(env, filters));
}

static void JNI_AdblockBridge_Natives_SetAllowlistContent(
    JNIEnv* env,
    const JavaParamRef<jstring>& allowlist) {
  AdblockService::GetInstance()->SetAllowlistContent(
      ConvertJavaStringToUTF8(env, allowlist));
}

static void JNI_AdblockBridge_Natives_Rebuild(JNIEnv* env) {
  AdblockService::GetInstance()->Rebuild();
}

static jlong JNI_AdblockBridge_Natives_GetBlockedCount(JNIEnv* env) {
  return static_cast<jlong>(AdblockService::GetInstance()->blocked_count());
}

static void JNI_AdblockBridge_Natives_ResetBlockedCount(JNIEnv* env) {
  AdblockService::GetInstance()->ResetBlockedCount();
}

static jlong JNI_AdblockBridge_Natives_GetNetworkRuleCount(JNIEnv* env) {
  return static_cast<jlong>(
      AdblockService::GetInstance()->network_rule_count());
}

static jlong JNI_AdblockBridge_Natives_GetCosmeticRuleCount(JNIEnv* env) {
  return static_cast<jlong>(
      AdblockService::GetInstance()->cosmetic_rule_count());
}
