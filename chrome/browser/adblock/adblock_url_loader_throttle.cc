// Copyright 2026 The Kiwi Reborn Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/adblock/adblock_url_loader_throttle.h"

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "chrome/browser/adblock/adblock_service.h"
#include "components/adblock/adblock_resource_types.h"
#include "components/adblock/adblock_rule_parser.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "net/base/net_errors.h"
#include "content/public/common/resource_type.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace {

uint32_t ResourceTypeToMask(int content_resource_type) {
  using ResourceType = content::ResourceType;
  switch (static_cast<ResourceType>(content_resource_type)) {
    case ResourceType::kMainFrame:
      return kiwi_adblock::kResourceDocument;
    case ResourceType::kSubFrame:
      return kiwi_adblock::kResourceSubdocument;
    case ResourceType::kStylesheet:
      return kiwi_adblock::kResourceStylesheet;
    case ResourceType::kScript:
      return kiwi_adblock::kResourceScript;
    case ResourceType::kImage:
      return kiwi_adblock::kResourceImage;
    case ResourceType::kFontResource:
      return kiwi_adblock::kResourceFont;
    case ResourceType::kObject:
      return kiwi_adblock::kResourceObject;
    case ResourceType::kMedia:
      return kiwi_adblock::kResourceMedia;
    case ResourceType::kXhr:
      return kiwi_adblock::kResourceXmlHttpRequest;
    case ResourceType::kPing:
      return kiwi_adblock::kResourcePing;
    case ResourceType::kWorker:
    case ResourceType::kSharedWorker:
    case ResourceType::kPrefetch:
    case ResourceType::kFavicon:
    case ResourceType::kServiceWorker:
    case ResourceType::kCspReport:
    default:
      return kiwi_adblock::kResourceOther;
  }
}

}  // namespace

AdblockURLLoaderThrottle::AdblockURLLoaderThrottle(
    const base::RepeatingCallback<content::WebContents*()>& wc_getter,
    int frame_tree_node_id)
    : wc_getter_(wc_getter), frame_tree_node_id_(frame_tree_node_id) {}

AdblockURLLoaderThrottle::~AdblockURLLoaderThrottle() = default;

std::string AdblockURLLoaderThrottle::ResolveDocumentHost(
    const network::ResourceRequest& request) const {
  // 1) The initiator origin is the document that started this request.
  if (request.request_initiator.has_value() &&
      request.request_initiator->GetURL().SchemeIsHTTPOrHTTPS()) {
    return request.request_initiator->GetURL().host();
  }
  // 2) Fall back to the referrer host.
  const GURL& referrer = request.referrer;
  if (referrer.SchemeIsHTTPOrHTTPS() && !referrer.host().empty()) {
    return referrer.host();
  }
  // 3) Fall back to the frame's committed document.
  content::WebContents* web_contents = wc_getter_.is_null()
                                           ? nullptr
                                           : wc_getter_.Run();
  if (web_contents) {
    content::RenderFrameHost* frame =
        frame_tree_node_id_ >= 0
            ? content::RenderFrameHost::FromFrameTreeNodeId(
                  frame_tree_node_id_)
            : web_contents->GetPrimaryMainFrame();
    if (frame) {
      const GURL doc_url = frame->GetLastCommittedURL();
      if (doc_url.SchemeIsHTTPOrHTTPS()) {
        return doc_url.host();
      }
    }
  }
  return std::string();
}

void AdblockURLLoaderThrottle::WillStartRequest(
    network::ResourceRequest* request,
    bool* defer) {
  if (!AdblockService::GetInstance()->IsEnabled()) {
    return;
  }
  const GURL& url = request->url;
  if (!url.SchemeIsHTTPOrHTTPS() || url.host().empty()) {
    return;
  }

  const std::string url_spec = base::ToLowerASCII(url.spec());
  const std::string request_host = url.host();
  const std::string document_host = ResolveDocumentHost(*request);

  // Third-party heuristic: the request host belongs to a different site
  // than the document host (subdomains count as same-party).
  bool third_party =
      !document_host.empty() &&
      !kiwi_adblock::HostMatchesDomain(request_host, document_host) &&
      !kiwi_adblock::HostMatchesDomain(document_host, request_host);

  const uint32_t mask = ResourceTypeToMask(request->resource_type);

  const kiwi_adblock::MatchResult result =
      AdblockService::GetInstance()->MatchNetwork(
          url_spec, request_host, document_host, mask, third_party);
  if (result.blocked) {
    delegate_->CancelWithError(net::ERR_BLOCKED_BY_CLIENT,
                               "KiwiAdblock");
  }
}
