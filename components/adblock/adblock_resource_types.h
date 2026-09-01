// Copyright 2026 The Kiwi Reborn Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_ADBLOCK_ADBLOCK_RESOURCE_TYPES_H_
#define COMPONENTS_ADBLOCK_ADBLOCK_RESOURCE_TYPES_H_

#include <cstdint>

namespace kiwi_adblock {

// Resource-type bitmask. Values mirror the ABP/EasyList `$option` list so the
// parser can translate filter options directly. The chrome-side throttle maps
// content::ResourceType onto this bitmask before calling the engine.
enum ResourceType : uint32_t {
  kResourceOther = 1u << 0,
  kResourceScript = 1u << 1,
  kResourceImage = 1u << 2,
  kResourceStylesheet = 1u << 3,
  kResourceSubdocument = 1u << 4,  // iframe
  kResourceObject = 1u << 5,       // object / embed
  kResourceXmlHttpRequest = 1u << 6,
  kResourceFont = 1u << 7,
  kResourceMedia = 1u << 8,  // <audio> / <video>
  kResourceWebSocket = 1u << 9,
  kResourceDocument = 1u << 10,  // main frame / popup target
  kResourcePing = 1u << 11,
  kResourceAll = 0xFFFFFFFFu,
};

}  // namespace kiwi_adblock

#endif  // COMPONENTS_ADBLOCK_ADBLOCK_RESOURCE_TYPES_H_
