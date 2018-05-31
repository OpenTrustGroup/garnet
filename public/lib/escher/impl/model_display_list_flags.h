// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_MODEL_DISPLAY_LIST_FLAGS_H_
#define LIB_ESCHER_IMPL_MODEL_DISPLAY_LIST_FLAGS_H_

#include <vulkan/vulkan.hpp>

#include "lib/escher/util/debug_print.h"

namespace escher {
namespace impl {

// Flags used to configure construction of a ModelDisplayList.
enum class ModelDisplayListFlag {
  kNull = 0,
  kSortByPipeline = 1 << 0,
  kDisableDepthTest = 1 << 1,
  kShareDescriptorSetsBetweenObjects = 1 << 2
};

using ModelDisplayListFlags = vk::Flags<ModelDisplayListFlag>;

inline ModelDisplayListFlags operator|(ModelDisplayListFlag bit0,
                                       ModelDisplayListFlag bit1) {
  return ModelDisplayListFlags(bit0) | bit1;
}

inline ModelDisplayListFlags operator~(ModelDisplayListFlag bit) {
  return ~ModelDisplayListFlags(bit);
}

}  // namespace impl

// Debugging.
ESCHER_DEBUG_PRINTABLE(impl::ModelDisplayListFlag);
ESCHER_DEBUG_PRINTABLE(impl::ModelDisplayListFlags);

}  // namespace escher

namespace vk {

template <>
struct FlagTraits<escher::impl::ModelDisplayListFlag> {
  enum {
    allFlags = VkFlags(escher::impl::ModelDisplayListFlag::kSortByPipeline) |
               VkFlags(escher::impl::ModelDisplayListFlag::kDisableDepthTest) |
               VkFlags(escher::impl::ModelDisplayListFlag::
                           kShareDescriptorSetsBetweenObjects)
  };
};

}  // namespace vk

#endif  // LIB_ESCHER_IMPL_MODEL_DISPLAY_LIST_FLAGS_H_
