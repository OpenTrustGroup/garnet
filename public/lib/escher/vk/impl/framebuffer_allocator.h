// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_VK_IMPL_FRAMEBUFFER_ALLOCATOR_H_
#define LIB_ESCHER_VK_IMPL_FRAMEBUFFER_ALLOCATOR_H_

#include "lib/escher/forward_declarations.h"
#include "lib/escher/util/hash_cache.h"
#include "lib/escher/vk/impl/framebuffer.h"

namespace escher {
namespace impl {

// FramebufferAllocator wraps HashCache to provide a frame-based cache for
// Vulkan framebuffers.  The eviction semantics are the same as a HashCache
// with FramesUntilEviction == 4.
class FramebufferAllocator {
 public:
  static constexpr uint32_t kFramesUntilEviction = 4;

  FramebufferAllocator(ResourceRecycler* recycler,
                       impl::RenderPassCache* render_pass_cache);
  const impl::FramebufferPtr& ObtainFramebuffer(const RenderPassInfo& info);

  void BeginFrame() { framebuffer_cache_.BeginFrame(); }
  void Clear() { framebuffer_cache_.Clear(); }

 private:
  struct CacheItem : public HashCacheItem<CacheItem> {
    impl::FramebufferPtr framebuffer;
  };

  ResourceRecycler* const recycler_;
  impl::RenderPassCache* const render_pass_cache_;
  HashCache<CacheItem, DefaultObjectPoolPolicy<CacheItem>, kFramesUntilEviction>
      framebuffer_cache_;
};

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_VK_IMPL_FRAMEBUFFER_ALLOCATOR_H_
