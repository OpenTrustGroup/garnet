// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_RENDERER_BUFFER_CACHE_H_
#define LIB_ESCHER_RENDERER_BUFFER_CACHE_H_

#include <list>
#include <map>
#include <vulkan/vulkan.hpp>

#include "lib/escher/escher.h"
#include "lib/escher/forward_declarations.h"
#include "lib/escher/resources/resource_recycler.h"
#include "lib/escher/vk/buffer.h"
#include "lib/escher/vk/gpu_allocator.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/time/time_point.h"

namespace escher {

using BufferCacheWeakPtr = fxl::WeakPtr<BufferCache>;
using GpuAllocatorWeakPtr = fxl::WeakPtr<GpuAllocator>;

// Allow client to obtain new or recycled Buffers backed by host GPU memory.
// All Buffers obtained from a BufferCache must be destroyed before the
// BufferCache is destroyed.
class BufferCache : public ResourceRecycler {
 public:
  explicit BufferCache(EscherWeakPtr escher);
  ~BufferCache() override;

  fxl::WeakPtr<BufferCache> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // Obtain an unused Buffer with the required properties.  A new Buffer might
  // be created, or an existing one reused.  NOTE: the buffer is not guaranteed
  // to be exactly the requested size; it may be larger.
  BufferPtr NewHostBuffer(vk::DeviceSize size);

  // |ResourceRecycler|
  void RecycleResource(std::unique_ptr<Resource> resource) override;

  size_t free_buffer_count() const { return free_buffers_by_id_.size(); }

 private:
  // The maximum amount of allocated memory cached in the BufferCache.
  // TODO() Optimize the maximum amount of memory to cache. Value was chosen
  // to match the amount of memory allocated by the GpuUploader by default.
  static constexpr size_t kMaxMemoryCached = 1024 * 1024;

  // Buffer usage info.
  // TODO(SCN-851) Grow this class to handle different buffer usage and memory
  // flags. It should work with the UniformBlockAllocator.
  const vk::BufferUsageFlags kUsageFlags =
      vk::BufferUsageFlagBits::eTransferSrc |
      vk::BufferUsageFlagBits::eTransferDst;
  const vk::MemoryPropertyFlags kMemoryPropertyFlags =
      vk::MemoryPropertyFlagBits::eHostVisible |
      vk::MemoryPropertyFlagBits::eHostCoherent;

  struct CacheInfo {
    uint64_t id;
    fxl::TimePoint allocation_time;
    vk::DeviceSize size;
  };

  // Represents an LRU cache of Buffers. Buffers are identified by their ID and
  // accessed from the map of free buffers by their size. The cache is pruned
  // when the working cache size exceeds kMaxMemoryCached.
  std::map<fxl::TimePoint, CacheInfo> free_buffer_cache_;
  std::map<uint64_t, CacheInfo> free_buffers_by_id_;
  size_t cache_size_ = 0;

  // Map of free buffers.
  std::map<vk::DeviceSize, std::list<std::unique_ptr<Buffer>>> free_buffers_;

  // Allocator for the buffers.
  GpuAllocatorWeakPtr gpu_allocator_;

  fxl::WeakPtrFactory<BufferCache> weak_factory_;  // must be last

  FXL_DISALLOW_COPY_AND_ASSIGN(BufferCache);
};

}  // namespace escher

#endif  // LIB_ESCHER_RENDERER_BATCH_GPU_UPLOADER_H_
