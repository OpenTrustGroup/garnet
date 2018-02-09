// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/vk/image.h"

#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/resources/resource_manager.h"
#include "lib/escher/vk/gpu_mem.h"

namespace escher {

const ResourceTypeInfo Image::kTypeInfo("Image",
                                        ResourceType::kResource,
                                        ResourceType::kWaitableResource,
                                        ResourceType::kImage);

ImagePtr Image::New(ResourceManager* image_owner,
                    ImageInfo info,
                    vk::Image vk_image,
                    GpuMemPtr mem,
                    vk::DeviceSize mem_offset,
                    bool bind_image_memory) {
  if (mem && bind_image_memory) {
    auto bind_result = image_owner->device().bindImageMemory(
        vk_image, mem->base(), mem->offset() + mem_offset);
    if (bind_result != vk::Result::eSuccess) {
      FXL_DLOG(ERROR) << "vkBindImageMemory failed: "
                      << vk::to_string(bind_result);
      return nullptr;
    }
  }
  return fxl::AdoptRef(new Image(image_owner, info, vk_image, mem, mem_offset));
}

Image::Image(ResourceManager* image_owner,
             ImageInfo info,
             vk::Image vk_image,
             GpuMemPtr mem,
             vk::DeviceSize mem_offset)
    : WaitableResource(image_owner),
      info_(info),
      image_(vk_image),
      mem_(std::move(mem)),
      mem_offset_(mem_offset) {
  // TODO: How do we future-proof this in case more formats are added?
  switch (info.format) {
    case vk::Format::eD16Unorm:
    case vk::Format::eX8D24UnormPack32:
    case vk::Format::eD32Sfloat:
      has_depth_ = true;
      has_stencil_ = false;
      break;
    case vk::Format::eS8Uint:
      has_depth_ = false;
      has_stencil_ = true;
      break;
    case vk::Format::eD16UnormS8Uint:
    case vk::Format::eD24UnormS8Uint:
    case vk::Format::eD32SfloatS8Uint:
      has_depth_ = true;
      has_stencil_ = true;
      break;
    default:
      // No depth or stencil component.
      has_depth_ = false;
      has_stencil_ = false;
      break;
  }
}

Image::~Image() {
  if (!mem_) {
    // Probably a swapchain image.  We don't own the image or the memory.
    FXL_LOG(INFO) << "Destroying Image with unowned VkImage (perhaps a "
                     "swapchain image?)";
  } else {
    vulkan_context().device.destroyImage(image_);
  }
}

}  // namespace escher
