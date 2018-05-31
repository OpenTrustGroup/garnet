// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_VK_SIMPLE_IMAGE_FACTORY_H_
#define LIB_ESCHER_VK_SIMPLE_IMAGE_FACTORY_H_

#include <vulkan/vulkan.hpp>

#include "lib/escher/impl/gpu_uploader.h"
#include "lib/escher/vk/image.h"
#include "lib/escher/vk/image_factory.h"
#include "lib/fxl/memory/ref_counted.h"

namespace escher {

// Creates images by allocating a new chunk of memory directly from the
// passed allocator. Does not cache images.
class SimpleImageFactory : public escher::ImageFactory {
 public:
  SimpleImageFactory(ResourceManager* resource_manager,
                     escher::GpuAllocator* allocator);
  ~SimpleImageFactory() override;

  ImagePtr NewImage(const escher::ImageInfo& info) override;

 private:
  ResourceManager* resource_manager_;
  escher::GpuAllocator* allocator_;
};

}  // namespace escher

#endif  // LIB_ESCHER_VK_SIMPLE_IMAGE_FACTORY_H_
