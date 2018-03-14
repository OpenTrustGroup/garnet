// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_RESOURCES_HOST_MEMORY_H_
#define GARNET_LIB_UI_SCENIC_RESOURCES_HOST_MEMORY_H_

#include <vulkan/vulkan.hpp>

#include "garnet/lib/ui/mozart/util/error_reporter.h"
#include "garnet/lib/ui/scenic/resources/memory.h"
#include "lib/escher/vk/gpu_mem.h"
#include "lib/fsl/vmo/shared_vmo.h"

namespace scene_manager {

class HostMemory;
using HostMemoryPtr = fxl::RefPtr<HostMemory>;

// Wraps a CPU host memory-backed VMO.
class HostMemory : public Memory {
 public:
  static const ResourceTypeInfo kTypeInfo;

  // Constructor for host memory.
  HostMemory(Session* session,
             scenic::ResourceId id,
             zx::vmo vmo,
             uint64_t vmo_size);

  // Helper method for creating HostMemory object from a scenic::Memory.
  // Create a HostMemory resource object from a CPU host memory-backed VMO.
  //
  // Returns the created HostMemory object or nullptr if there was an error.
  static HostMemoryPtr New(Session* session,
                           scenic::ResourceId id,
                           vk::Device device,
                           zx::vmo vmo,
                           mz::ErrorReporter* error_reporter);

  // Helper method that calls the above method with the VMO from |args|. Also
  // checks the memory type in debug mode.
  static HostMemoryPtr New(Session* session,
                           scenic::ResourceId id,
                           vk::Device device,
                           const scenic::MemoryPtr& args,
                           mz::ErrorReporter* error_reporter);

  void Accept(class ResourceVisitor* visitor) override;

  void* memory_base() { return shared_vmo_->Map(); }
  uint64_t size() const { return size_; }

 private:
  fxl::RefPtr<fsl::SharedVmo> shared_vmo_;
  uint64_t size_;
};

}  // namespace scene_manager

#endif  // GARNET_LIB_UI_SCENIC_RESOURCES_HOST_MEMORY_H_
