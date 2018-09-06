// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/vk/gpu_allocator.h"

#include "lib/escher/impl/vulkan_utils.h"

namespace escher {

GpuAllocator::GpuAllocator(const VulkanContext& context)
    : physical_device_(context.physical_device),
      device_(context.device),
      weak_factory_(this) {}

GpuAllocator::~GpuAllocator() {
  FXL_CHECK(total_slab_bytes_ == 0);
  FXL_CHECK(slab_count_ == 0);
}

GpuMemPtr GpuAllocator::AllocateSlab(vk::MemoryRequirements reqs,
                                     vk::MemoryPropertyFlags flags) {
  return impl::GpuMemSlab::New(device(), physical_device(), reqs, flags, this);
}

void GpuAllocator::OnSlabCreated(vk::DeviceSize slab_size) {
  ++slab_count_;
  total_slab_bytes_ += slab_size;
}

void GpuAllocator::OnSlabDestroyed(vk::DeviceSize slab_size) {
  --slab_count_;
  total_slab_bytes_ -= slab_size;
}

}  // namespace escher
