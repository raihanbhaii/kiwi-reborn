// Copyright 2026 The Kiwi Reborn Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ADBLOCK_ADBLOCK_URL_LOADER_THROTTLE_H_
#define CHROME_BROWSER_ADBLOCK_ADBLOCK_URL_LOADER_THROTTLE_H_

#include "base/memory/weak_ptr.h"
#include "third_party/blink/public/common/loader/url_loader_throttle.h"

namespace content {
class WebContents;
}

// Network-level request blocker, Brave-style: every request that flows
// through the browser's network service passes WillStartRequest() here and
// is checked against the filter engine before any connection is made.
class AdblockURLLoaderThrottle : public blink::URLLoaderThrottle {
 public:
  // `wc_getter` resolves the WebContents the request belongs to (may return
  // null for prerenders / detached requests); `frame_tree_node_id` is used
  // to find the frame's document when the initiator is missing.
  AdblockURLLoaderThrottle(
      const base::RepeatingCallback<content::WebContents*()>& wc_getter,
      int frame_tree_node_id);
  ~AdblockURLLoaderThrottle() override;

  void WillStartRequest(network::ResourceRequest* request,
                        bool* defer) override;

 private:
  // Host of the top-level document for third-party checks.
  std::string ResolveDocumentHost(
      const network::ResourceRequest& request) const;

  base::RepeatingCallback<content::WebContents*()> wc_getter_;
  int frame_tree_node_id_;
};

#endif  // CHROME_BROWSER_ADBLOCK_ADBLOCK_URL_LOADER_THROTTLE_H_
