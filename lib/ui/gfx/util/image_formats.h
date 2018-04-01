// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_UTIL_IMAGE_FORMATS_H_
#define GARNET_LIB_UI_GFX_UTIL_IMAGE_FORMATS_H_

#include <fuchsia/cpp/images.h>
#include "lib/escher/util/image_utils.h"

// Contains utilities for converting from various formats to BGRA_8, which is
// what is needed to render.
// TODO(MZ-547): Merge with existing image conversion libraries in media:
// bin/media/video/video_converter.h

namespace scenic {
namespace gfx {
namespace image_formats {

// Returns the number of bytes per pixel for the given format.
size_t BytesPerPixel(const images::PixelFormat& pixel_format);

// Returns the pixel alignment for the given format.
size_t PixelAlignment(const images::PixelFormat& pixel_format);

// Returns a function that can be used to convert any format supported in
// ImageInfo into a BGRA_8 image.
escher::image_utils::ImageConversionFunction GetFunctionToConvertToBgra8(
    const ::images::ImageInfo& image_info);

}  // namespace image_formats
}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_UTIL_IMAGE_FORMATS_H_
