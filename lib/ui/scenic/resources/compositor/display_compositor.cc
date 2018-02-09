// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/resources/compositor/display_compositor.h"

#include "garnet/lib/ui/scenic/displays/display.h"
#include "garnet/lib/ui/scenic/swapchain/swapchain.h"

namespace scene_manager {

const ResourceTypeInfo DisplayCompositor::kTypeInfo = {
    ResourceType::kCompositor | ResourceType::kDisplayCompositor,
    "DisplayCompositor"};

DisplayCompositor::DisplayCompositor(Session* session,
                                     scenic::ResourceId id,
                                     Display* display,
                                     std::unique_ptr<Swapchain> swapchain)
    : Compositor(session,
                 id,
                 DisplayCompositor::kTypeInfo,
                 std::move(swapchain)),
      display_(display) {
  FXL_DCHECK(display_);
}

DisplayCompositor::~DisplayCompositor() = default;

}  // namespace scene_manager
