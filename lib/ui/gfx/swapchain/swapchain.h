// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_SWAPCHAIN_SWAPCHAIN_H_
#define GARNET_LIB_UI_GFX_SWAPCHAIN_SWAPCHAIN_H_

#include <lib/fit/function.h>

#include "lib/fxl/memory/ref_ptr.h"

namespace escher {
class Image;
class Semaphore;
using ImagePtr = fxl::RefPtr<Image>;
using SemaphorePtr = fxl::RefPtr<Semaphore>;
};  // namespace escher

namespace scenic {
namespace gfx {

class FrameTimings;
using FrameTimingsPtr = fxl::RefPtr<FrameTimings>;

// Swapchain is an interface used used to render into an escher::Image and
// present the result (to a physical display or elsewhere).
class Swapchain {
 public:
  // Callback used to draw a frame. Arguments are:
  // - the framebuffer to render into.
  // - the semaphore to wait upon before rendering into the framebuffer
  // - the semaphore to signal when rendering is complete.
  using DrawCallback =
      fit::function<void(const escher::ImagePtr&, const escher::SemaphorePtr&,
                         const escher::SemaphorePtr&)>;

  // Returns false if the frame could not be drawn.  Otherwise,
  //   1. Registers itself with |frame_timings| using
  //      FrameTimings::AddSwapchain().
  //   2. Invokes |draw_callback| to draw the frame.
  //   3. Eventually invokes FrameTimings::OnFrameFinishedRendering() and
  //      FrameTimings::OnFramePresented() on |frame_timings|.
  virtual bool DrawAndPresentFrame(const FrameTimingsPtr& frame_timings,
                                   DrawCallback draw_callback) = 0;

  virtual ~Swapchain() = default;
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_SWAPCHAIN_SWAPCHAIN_H_
