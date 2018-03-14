// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_SCENIC_SYSTEM_H_
#define GARNET_LIB_UI_SCENIC_SCENIC_SYSTEM_H_

#include "garnet/lib/ui/mozart/system.h"
#include "garnet/lib/ui/scenic/displays/display_manager.h"
#include "garnet/lib/ui/scenic/engine/engine.h"
#include "lib/escher/escher.h"

namespace scene_manager {

class ScenicSystem : public mz::TempSystemDelegate {
 public:
  static constexpr TypeId kTypeId = kScenic;

  explicit ScenicSystem(mz::SystemContext context);
  ~ScenicSystem();

  std::unique_ptr<mz::CommandDispatcher> CreateCommandDispatcher(
      mz::CommandDispatcherContext context) override;

  // TODO(MZ-452): Remove this when we externalize Displays.
  void GetDisplayInfo(
      const ui_mozart::Mozart::GetDisplayInfoCallback& callback) override;

 private:
  void Initialize();

  DisplayManager display_manager_;

  // TODO(MZ-452): Remove this when we externalize Displays.
  void GetDisplayInfoImmediately(
      const ui_mozart::Mozart::GetDisplayInfoCallback& callback);

  // TODO(MZ-452): Remove this when we externalize Displays.
  bool initialized_ = false;
  std::vector<fxl::Closure> run_after_initialized_;

  escher::VulkanInstancePtr vulkan_instance_;
  escher::VulkanDeviceQueuesPtr vulkan_device_queues_;
  vk::SurfaceKHR surface_;
  std::unique_ptr<escher::Escher> escher_;
  std::unique_ptr<Engine> engine_;
};

}  // namespace scene_manager

#endif  // GARNET_LIB_UI_SCENIC_SCENIC_SYSTEM_H_
