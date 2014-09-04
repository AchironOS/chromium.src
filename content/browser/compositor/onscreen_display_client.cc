// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/compositor/onscreen_display_client.h"

#include "base/debug/trace_event.h"
#include "cc/output/output_surface.h"
#include "cc/surfaces/surface_factory.h"
#include "cc/surfaces/surface_manager.h"
#include "content/common/host_shared_bitmap_manager.h"

namespace content {

OnscreenDisplayClient::OnscreenDisplayClient(
    scoped_ptr<cc::OutputSurface> output_surface,
    cc::SurfaceManager* manager,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner)
    : output_surface_(output_surface.Pass()),
      display_(
          new cc::Display(this, manager, HostSharedBitmapManager::current())),
      task_runner_(task_runner),
      scheduled_draw_(false),
      weak_ptr_factory_(this) {
}

OnscreenDisplayClient::~OnscreenDisplayClient() {
}

scoped_ptr<cc::OutputSurface> OnscreenDisplayClient::CreateOutputSurface() {
  DCHECK(output_surface_.get());
  return output_surface_.Pass();
}

void OnscreenDisplayClient::DisplayDamaged() {
  if (scheduled_draw_)
    return;
  TRACE_EVENT0("content", "OnscreenDisplayClient::DisplayDamaged");
  scheduled_draw_ = true;
  task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&OnscreenDisplayClient::Draw, weak_ptr_factory_.GetWeakPtr()));
}

void OnscreenDisplayClient::Draw() {
  TRACE_EVENT0("content", "OnscreenDisplayClient::Draw");
  scheduled_draw_ = false;
  display_->Draw();
}

}  // namespace content
