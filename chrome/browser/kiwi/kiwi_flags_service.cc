// Copyright 2026 The Kiwi Reborn Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/kiwi/kiwi_flags_service.h"

#include "base/no_destructor.h"

// static
KiwiFlagsService* KiwiFlagsService::GetInstance() {
  static base::NoDestructor<KiwiFlagsService> instance;
  return instance.get();
}
