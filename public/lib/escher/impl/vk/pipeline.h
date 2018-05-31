// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_VK_PIPELINE_H_
#define LIB_ESCHER_IMPL_VK_PIPELINE_H_

#include <vulkan/vulkan.hpp>

#include "lib/escher/impl/vk/pipeline_layout.h"
#include "lib/escher/impl/vk/pipeline_spec.h"
#include "lib/fxl/memory/ref_counted.h"

namespace escher {
namespace impl {

// Manages the lifecycle of Vulkan Pipelines and PipelineLayouts.
class Pipeline : public fxl::RefCountedThreadSafe<Pipeline> {
 public:
  // The vk::Pipeline becomes owned by this Pipeline instance, and is destroyed
  // in the destructor.  The vk::Device is not owned; it is used for destroying
  // the pipeline.
  Pipeline(vk::Device device, vk::Pipeline pipeline, PipelineLayoutPtr layout,
           PipelineSpec spec);
  ~Pipeline();

  vk::Pipeline vk() const { return pipeline_; }
  vk::PipelineLayout vk_layout() const { return layout_->vk(); }
  const PipelineSpec& spec() const { return spec_; }

 private:
  vk::Device device_;
  vk::Pipeline pipeline_;
  PipelineLayoutPtr layout_;
  PipelineSpec spec_;
};

typedef fxl::RefPtr<Pipeline> PipelinePtr;

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_IMPL_VK_PIPELINE_H_
