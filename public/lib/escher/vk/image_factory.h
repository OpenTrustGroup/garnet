// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_VK_IMAGE_FACTORY_H_
#define LIB_ESCHER_VK_IMAGE_FACTORY_H_

#include "lib/escher/vk/image.h"
#include "lib/fxl/memory/ref_counted.h"

namespace escher {

// ImageFactory creates Images, which may or may not have been recycled.  All
// Images obtained from an ImageFactory must be destroyed before the
// ImageFactory is destroyed.
class ImageFactory {
 public:
  virtual ~ImageFactory() {}
  virtual ImagePtr NewImage(const ImageInfo& info) = 0;
};

}  // namespace escher

#endif  // LIB_ESCHER_VK_IMAGE_FACTORY_H_
