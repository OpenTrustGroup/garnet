// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_RESOURCES_MEMORY_H_
#define GARNET_LIB_UI_SCENIC_RESOURCES_MEMORY_H_

#include <vulkan/vulkan.hpp>

#include "garnet/lib/ui/scenic/resources/resource.h"
#include "garnet/lib/ui/scenic/util/error_reporter.h"
#include "lib/escher/vk/gpu_mem.h"

namespace scene_manager {

// Base class for Resource objects that wrap memory. Subclassed by GpuMemory
// and HostMemory.
class Memory : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

 protected:
  Memory(Session* session,
         scenic::ResourceId id,
         const ResourceTypeInfo& type_info);
};

using MemoryPtr = fxl::RefPtr<Memory>;

}  // namespace scene_manager

#endif  // GARNET_LIB_UI_SCENIC_RESOURCES_MEMORY_H_
