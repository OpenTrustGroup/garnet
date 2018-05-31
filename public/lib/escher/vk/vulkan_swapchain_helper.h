// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_VK_VULKAN_SWAPCHAIN_HELPER_H_
#define LIB_ESCHER_VK_VULKAN_SWAPCHAIN_HELPER_H_

#include "lib/escher/forward_declarations.h"
#include "lib/escher/vk/vulkan_swapchain.h"

namespace escher {

class PaperRenderer;

// TODO(ES-27): VulkanSwapchainHelper is now PaperRenderer-specific.
// Generalize or get rid of it.
class VulkanSwapchainHelper {
 public:
  VulkanSwapchainHelper(VulkanSwapchain swapchain,
                        vk::Device device,
                        vk::Queue queue);
  ~VulkanSwapchainHelper();

  // Calls EndFrame() after drawing.
  void DrawFrame(const FramePtr& frame,
                 PaperRenderer* renderer,
                 const Stage& stage,
                 const Model& model,
                 const Camera& camera,
                 const ShadowMapPtr& shadow_map,
                 const Model* overlay_model = nullptr);

  const VulkanSwapchain& swapchain() const { return swapchain_; }

 private:
  VulkanSwapchain swapchain_;
  vk::Device device_;
  vk::Queue queue_;

  size_t next_semaphore_index_ = 0;
  std::vector<SemaphorePtr> image_available_semaphores_;
  std::vector<SemaphorePtr> render_finished_semaphores_;

  FXL_DISALLOW_COPY_AND_ASSIGN(VulkanSwapchainHelper);
};

}  // namespace escher

#endif  // LIB_ESCHER_VK_VULKAN_SWAPCHAIN_HELPER_H_
