// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_GUEST_VIEW_WEB_VIEW_WEB_VIEW_GUEST_DELEGATE_H_
#define EXTENSIONS_BROWSER_GUEST_VIEW_WEB_VIEW_WEB_VIEW_GUEST_DELEGATE_H_

#include "extensions/browser/guest_view/guest_view_base.h"

namespace blink {
struct WebFindOptions;
}  // nanespace blink

namespace content {
class RenderViewHost;
class WebContents;
}

namespace extensions {

class WebViewGuest;
class WebViewInternalFindFunction;

namespace api {
namespace web_view_internal{
struct ContextMenuItem;
}  // namespace web_view_internal
}  // namespace api

// A delegate class of WebViewGuest that are not a part of chrome.
class WebViewGuestDelegate {
 public :
  explicit WebViewGuestDelegate(WebViewGuest* web_view_guest);
  virtual ~WebViewGuestDelegate();

  typedef std::vector<linked_ptr<api::web_view_internal::ContextMenuItem> >
      MenuItemVector;

  // Begin or continue a find request.
  virtual void Find(
      const base::string16& search_text,
      const blink::WebFindOptions& options,
      WebViewInternalFindFunction* find_function) = 0;

  // Result of string search in the page.
  virtual void FindReply(content::WebContents* source,
                         int request_id,
                         int number_of_matches,
                         const gfx::Rect& selection_rect,
                         int active_match_ordinal,
                         bool final_update) = 0;

  // Returns the current zoom factor.
  virtual double GetZoom() = 0;

  // Called when context menu operation was handled.
  virtual bool HandleContextMenu(const content::ContextMenuParams& params) = 0;

  // Called to attach helpers just after additional initialization is performed.
  virtual void OnAttachWebViewHelpers(content::WebContents* contents) = 0;

  // Called to perform some cleanup prior to destruction.
  virtual void OnEmbedderDestroyed() = 0;

  // Called when the guest WebContents commits a provisional load in any frame.
  virtual void OnDidCommitProvisionalLoadForFrame(bool is_main_frame) = 0;

  // Called just after additional initialization is performed.
  virtual void OnDidInitialize() = 0;

  virtual void OnDocumentLoadedInFrame(
      content::RenderFrameHost* render_frame_host) = 0;

  // Called immediately after the guest WebContents has been destroyed.
  virtual void OnGuestDestroyed() = 0;

  // Called to cancel all find sessions in progress.
  virtual void OnRenderProcessGone() = 0;

  // Called when to set the zoom factor.
  virtual void OnSetZoom(double zoom_factor) = 0;

  // Shows the context menu for the guest.
  // |items| acts as a filter. This restricts the current context's default
  // menu items to contain only the items from |items|.
  // |items| == NULL means no filtering will be applied.
  virtual void OnShowContextMenu(
      int request_id,
      const MenuItemVector* items) = 0;

  // Conclude a find request to clear highlighting.
  virtual void StopFinding(content::StopFindAction) = 0;

  WebViewGuest* web_view_guest() const { return web_view_guest_; }

 private:
  WebViewGuest* const web_view_guest_;

  DISALLOW_COPY_AND_ASSIGN(WebViewGuestDelegate);
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_GUEST_VIEW_WEB_VIEW_WEB_VIEW_GUEST_DELEGATE_H_
