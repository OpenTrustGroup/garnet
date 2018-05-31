// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_VK_PIPELINE_LAYOUT_H_
#define LIB_ESCHER_IMPL_VK_PIPELINE_LAYOUT_H_

#include <vulkan/vulkan.hpp>

#include "lib/fxl/memory/ref_counted.h"

namespace escher {
namespace impl {

// Manages the lifecycle of a Vulkan PipelineLayout.
class PipelineLayout : public fxl::RefCountedThreadSafe<PipelineLayout> {
 public:
  // The vk::PipelineLayout becomes owned by this PipelineLayout instance, and
  // is destroyed.  The vk::Device is not owned; it is used for destroying
  // the pipeline layout.
  PipelineLayout(vk::Device device, vk::PipelineLayout layout);
  ~PipelineLayout();

  vk::PipelineLayout vk() const { return layout_; }

 private:
  vk::Device device_;
  vk::PipelineLayout layout_;
};

typedef fxl::RefPtr<PipelineLayout> PipelineLayoutPtr;

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_IMPL_VK_PIPELINE_LAYOUT_H_
