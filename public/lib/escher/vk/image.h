// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_VK_IMAGE_H_
#define LIB_ESCHER_VK_IMAGE_H_

#include "lib/escher/forward_declarations.h"
#include "lib/escher/renderer/semaphore.h"
#include "lib/escher/resources/waitable_resource.h"
#include "lib/escher/util/debug_print.h"

namespace escher {

// Full description of the size and layout of an Image.
#pragma pack(push, 1)  // As required by escher::HashMapHasher<ImageInfo>
struct ImageInfo {
  vk::Format format = vk::Format::eUndefined;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t sample_count = 1;
  vk::ImageUsageFlags usage;
  vk::MemoryPropertyFlags memory_flags =
      vk::MemoryPropertyFlagBits::eDeviceLocal;
  vk::ImageTiling tiling = vk::ImageTiling::eOptimal;

  bool operator==(const ImageInfo& other) const {
    return format == other.format && width == other.width &&
           height == other.height && sample_count == other.sample_count &&
           usage == other.usage && memory_flags == other.memory_flags;
  }

  // Transient images are neither loaded nor stored by render passes.  Instead
  // they may be rendered into by one subpass and used as an input attachment by
  // a subsequent pass.  Consequently, it may be possible (not implemented yet,
  // and depending on the hardware/driver) to avoid allocating any memory for
  // such images, so that it exists only transiently in tile-local storage.
  bool is_transient() const {
    return static_cast<bool>(usage &
                             vk::ImageUsageFlagBits::eTransientAttachment);
  }
};
#pragma pack(pop)

// An Image is a WaitableResource that encapsulates a vk::Image.
class Image : public WaitableResource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  // Constructor.  In some cases it is necessary to wrap an un-owned vk::Image,
  // which should not be destroyed when this Image is destroyed (e.g. when
  // working with images associated with a vk::SwapchainKHR); this is done by
  // passing nullptr as the |mem| argument.
  // If |mem| is passed and |bind_image_memory| is true, this method also binds
  // the memory to the image. |mem_offset| is the offset of the image's memory
  // within |mem|.
  static ImagePtr New(ResourceManager* image_owner, ImageInfo info, vk::Image,
                      GpuMemPtr mem, vk::DeviceSize mem_offset = 0,
                      bool bind_image_memory = true);

  static ImagePtr New(ResourceManager* image_owner, const ImageInfo& info,
                      GpuAllocator* allocator);

  // Returns image_ and mem_ to the owner.
  ~Image() override;

  const ImageInfo& info() const { return info_; }
  vk::Image vk() const { return image_; }
  vk::Format format() const { return info_.format; }
  // TODO(ES-82): decide whether to deprecate format().
  vk::Format vk_format() const { return info_.format; }
  uint32_t width() const { return info_.width; }
  uint32_t height() const { return info_.height; }
  bool has_depth() const { return has_depth_; }
  bool has_stencil() const { return has_stencil_; }
  bool is_transient() const { return info_.is_transient(); }
  const GpuMemPtr& memory() const { return mem_; }
  // Offset of the Image within its GpuMem.
  vk::DeviceSize memory_offset() const { return mem_offset_; }

  // TODO(ES-83): how does this interact with swapchain_layout_?
  // Should this be automatically set when various transitions are made, e.g.
  // finishing a render-pass?  Should it be locked so that the layout can't be
  // changed during a render-pass where it is used as an attachment?
  void set_layout(vk::ImageLayout new_layout) { layout_ = new_layout; }
  vk::ImageLayout layout() const { return layout_; }

  // Specify the layout that should be transitioned to when this image is used
  // as a framebuffer attachment.
  void set_swapchain_layout(vk::ImageLayout layout) {
    swapchain_layout_ = layout;
  }
  vk::ImageLayout swapchain_layout() const { return swapchain_layout_; }
  bool is_swapchain_image() const {
    return swapchain_layout_ != vk::ImageLayout::eUndefined;
  }

 protected:
  // Constructor.  In some cases it is necessary to wrap an un-owned vk::Image,
  // which should not be destroyed when this Image is destroyed (e.g. when
  // working with images associated with a vk::SwapchainKHR); this is done by
  // passing nullptr as the |mem| argument.
  // |mem_offset| is the offset of the image's memory within |mem|.
  Image(ResourceManager* image_owner, ImageInfo info, vk::Image, GpuMemPtr mem,
        vk::DeviceSize mem_offset);

 private:
  const ImageInfo info_;
  const vk::Image image_;
  GpuMemPtr mem_;
  const vk::DeviceSize mem_offset_ = 0;
  bool has_depth_;
  bool has_stencil_;

  // TODO(ES-83): consider allowing image to have an initial layout.
  vk::ImageLayout layout_ = vk::ImageLayout::eUndefined;

  vk::ImageLayout swapchain_layout_ = vk::ImageLayout::eUndefined;

  FXL_DISALLOW_COPY_AND_ASSIGN(Image);
};

typedef fxl::RefPtr<Image> ImagePtr;

// Debugging.
ESCHER_DEBUG_PRINTABLE(ImageInfo);

}  // namespace escher

#endif  // LIB_ESCHER_VK_IMAGE_H_
