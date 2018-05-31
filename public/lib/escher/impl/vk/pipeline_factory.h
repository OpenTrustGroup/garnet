// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_VK_PIPELINE_FACTORY_H_
#define LIB_ESCHER_IMPL_VK_PIPELINE_FACTORY_H_

#include <future>

#include "lib/escher/impl/vk/pipeline.h"
#include "lib/fxl/memory/ref_counted.h"

namespace escher {
namespace impl {

// Interface which can be used to create a new Pipeline, given a PipelineSpec.
// The "type" field of the PipelineSpec allows the PipelineFactory to quickly
// decide whether it is able to create the requested pipeline, and also to
// validate whether the spec's "data" is compatible with the requested "type".
class PipelineFactory : public fxl::RefCountedThreadSafe<PipelineFactory> {
 public:
  virtual ~PipelineFactory() {}
  virtual std::future<PipelinePtr> NewPipeline(PipelineSpec spec) = 0;
};

typedef fxl::RefPtr<PipelineFactory> PipelineFactoryPtr;

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_IMPL_VK_PIPELINE_FACTORY_H_
