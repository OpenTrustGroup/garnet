// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/vk/descriptor_set_allocator.h"

#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/vk/vulkan_limits.h"

namespace escher {

DescriptorSetAllocator::DescriptorSetAllocator(vk::Device device,
                                               DescriptorSetLayout layout)
    : cache_(device, layout) {}

DescriptorSetAllocator::PoolPolicy::PoolPolicy(vk::Device device,
                                               DescriptorSetLayout layout)
    : vk_device_(device), layout_(layout) {
  FXL_DCHECK(layout.IsValid());

  std::array<vk::DescriptorSetLayoutBinding, VulkanLimits::kNumBindings>
      bindings;
  size_t num_bindings = 0;

  for (uint32_t i = 0; i < VulkanLimits::kNumBindings; i++) {
    uint32_t index_mask = 1u << i;

    if (index_mask & layout.sampled_image_mask) {
      bindings[num_bindings++] = {i, vk::DescriptorType::eCombinedImageSampler,
                                  1, layout.stages, nullptr};
      pool_sizes_.push_back({vk::DescriptorType::eCombinedImageSampler, 0});
      continue;
    }

    if (index_mask & layout.sampled_buffer_mask) {
      bindings[num_bindings++] = {i, vk::DescriptorType::eUniformTexelBuffer, 1,
                                  layout.stages, nullptr};
      pool_sizes_.push_back({vk::DescriptorType::eUniformTexelBuffer, 0});
      continue;
    }

    if (index_mask & layout.storage_image_mask) {
      bindings[num_bindings++] = {i, vk::DescriptorType::eStorageImage, 1,
                                  layout.stages, nullptr};
      pool_sizes_.push_back({vk::DescriptorType::eStorageImage, 0});
      continue;
    }

    // TODO(SCN-699): Consider allowing both static and dynamic offsets for
    // uniform buffers.
    if (index_mask & layout.uniform_buffer_mask) {
      bindings[num_bindings++] = {i, vk::DescriptorType::eUniformBufferDynamic,
                                  1, layout.stages, nullptr};
      pool_sizes_.push_back({vk::DescriptorType::eUniformBufferDynamic, 0});
      continue;
    }

    // TODO(SCN-699): Consider allowing both static and dynamic offsets for
    // storage buffers.
    if (index_mask & layout.storage_buffer_mask) {
      bindings[num_bindings++] = {i, vk::DescriptorType::eStorageBuffer, 1,
                                  layout.stages, nullptr};
      pool_sizes_.push_back({vk::DescriptorType::eStorageBuffer, 0});
      continue;
    }

    if (index_mask & layout.input_attachment_mask) {
      bindings[num_bindings++] = {i, vk::DescriptorType::eInputAttachment, 1,
                                  layout.stages, nullptr};
      pool_sizes_.push_back({vk::DescriptorType::eInputAttachment, 0});
      continue;
    }
  }
  FXL_DCHECK(num_bindings);

  vk::DescriptorSetLayoutCreateInfo info;
  info.bindingCount = num_bindings;
  info.pBindings = bindings.data();

  vk_layout_ = ESCHER_CHECKED_VK_RESULT(device.createDescriptorSetLayout(info));
}

DescriptorSetAllocator::PoolPolicy::~PoolPolicy() {
  FXL_DCHECK(pools_.empty());
  vk_device_.destroyDescriptorSetLayout(vk_layout_);
}

void DescriptorSetAllocator::PoolPolicy::InitializePoolObjectBlock(
    CacheItem* objects,
    size_t block_index,
    size_t num_objects) {
  vk::DescriptorPool pool = CreatePool(block_index, num_objects);
  AllocateDescriptorSetBlock(pool, objects, num_objects);
}

vk::DescriptorPool DescriptorSetAllocator::PoolPolicy::CreatePool(
    size_t block_index,
    size_t num_objects) {
  FXL_DCHECK(!pools_[block_index]);
  for (auto& sz : pool_sizes_) {
    sz.descriptorCount = num_objects;
  }

  vk::DescriptorPoolCreateInfo info;
  info.maxSets = num_objects;
  if (!pool_sizes_.empty()) {
    info.poolSizeCount = pool_sizes_.size();
    info.pPoolSizes = pool_sizes_.data();
  }
  auto pool = ESCHER_CHECKED_VK_RESULT(vk_device_.createDescriptorPool(info));
  pools_[block_index] = pool;
  return pool;
}

void DescriptorSetAllocator::PoolPolicy::AllocateDescriptorSetBlock(
    vk::DescriptorPool pool,
    CacheItem* objects,
    size_t num_objects) {
  constexpr size_t kSetsPerAllocation = 64;
  std::array<vk::DescriptorSetLayout, kSetsPerAllocation> layouts;
  for (auto& layout : layouts) {
    layout = vk_layout_;
  }
  std::array<vk::DescriptorSet, kSetsPerAllocation> allocated_sets;

  size_t remaining = num_objects;
  vk::DescriptorSetAllocateInfo alloc_info;
  alloc_info.descriptorPool = pool;
  alloc_info.pSetLayouts = layouts.data();
  while (remaining) {
    alloc_info.descriptorSetCount = std::min(remaining, kSetsPerAllocation);

    vk::Result result =
        vk_device_.allocateDescriptorSets(&alloc_info, allocated_sets.data());
    FXL_CHECK(result == vk::Result::eSuccess)
        << "DescriptorSetAllocator failed to allocate block.";

    for (size_t i = 0; i < alloc_info.descriptorSetCount; ++i) {
      new (objects + i) CacheItem();
      objects[i].set = allocated_sets[i];
    }

    remaining -= alloc_info.descriptorSetCount;
    objects += alloc_info.descriptorSetCount;
  }
}

void DescriptorSetAllocator::PoolPolicy::DestroyPoolObjectBlock(
    CacheItem* objects,
    size_t block_index,
    size_t num_objects) {
  auto it = pools_.find(block_index);
  if (it == pools_.end()) {
    FXL_DCHECK(false)
        << "DescriptorSetAllocator could not find pool to destroy.";
    return;
  }
  vk::DescriptorPool pool = it->second;
  pools_.erase(it);
  FXL_DCHECK(pool);
  vk_device_.resetDescriptorPool(pool);
  vk_device_.destroyDescriptorPool(pool);

  // This isn't necessary, but do it anyway in case CacheItem is someday changed
  // to include values that require destruction.
  for (size_t i = 0; i < num_objects; ++i) {
    objects[i].~CacheItem();
  }
}

}  // namespace escher
