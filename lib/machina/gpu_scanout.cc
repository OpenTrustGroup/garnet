// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/gpu_scanout.h"

#include "garnet/lib/machina/gpu_resource.h"

namespace machina {

void GpuScanout::FlushRegion(const virtio_gpu_rect_t& rect) {
  GpuResource* res = resource_;
  if (res == nullptr) {
    return;
  }

  GpuRect source_rect;
  source_rect.x = rect.x;
  source_rect.y = rect.y;
  source_rect.width = rect.width;
  source_rect.height = rect.height;

  GpuRect dest_rect;
  dest_rect.x = rect_.x + rect.x;
  dest_rect.y = rect_.y + rect.y;
  dest_rect.width = rect.width;
  dest_rect.height = rect.height;

  surface_.DrawBitmap(res->bitmap(), source_rect, dest_rect);
}

void GpuScanout::SetResource(GpuResource* res,
                             const virtio_gpu_set_scanout_t* request) {
  GpuResource* old_res = resource_;
  resource_ = res;
  if (resource_ == nullptr) {
    if (old_res != nullptr) {
      old_res->DetachFromScanout();
    }
    return;
  }

  resource_->AttachToScanout(this);
  rect_.x = request->r.x;
  rect_.y = request->r.y;
  rect_.width = request->r.width;
  rect_.height = request->r.height;
}

void GpuScanout::WhenReady(OnReadyCallback callback) {
  ready_callback_ = fbl::move(callback);
  InvokeReadyCallback();
}

void GpuScanout::SetReady(bool ready) {
  ready_ = ready;
  InvokeReadyCallback();
}

void GpuScanout::InvokeReadyCallback() {
  if (ready_ && ready_callback_) {
    ready_callback_();
    ready_callback_ = nullptr;
  }
}

}  // namespace machina
