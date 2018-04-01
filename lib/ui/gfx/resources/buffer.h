// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_BUFFER_H_
#define GARNET_LIB_UI_GFX_RESOURCES_BUFFER_H_

#include "garnet/lib/ui/gfx/resources/gpu_memory.h"
#include "lib/escher/vk/buffer.h"

namespace scenic {
namespace gfx {

// Wraps a Vulkan buffer object.
class Buffer : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  Buffer(Session* session,
         scenic::ResourceId id,
         GpuMemoryPtr memory,
         uint32_t size,
         uint32_t offset);

  void Accept(class ResourceVisitor* visitor) override;

  const GpuMemoryPtr& memory() const { return memory_; }
  const escher::BufferPtr& escher_buffer() const { return escher_buffer_; }
  vk::DeviceSize size() const { return escher_buffer_->size(); }

 private:
  GpuMemoryPtr memory_;
  escher::BufferPtr escher_buffer_;
};

using BufferPtr = fxl::RefPtr<Buffer>;

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_BUFFER_H_
