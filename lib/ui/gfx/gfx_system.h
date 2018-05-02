// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_GFX_SYSTEM_H_
#define GARNET_LIB_UI_GFX_GFX_SYSTEM_H_

#include "garnet/lib/ui/gfx/displays/display_manager.h"
#include "garnet/lib/ui/gfx/engine/engine.h"
#include "garnet/lib/ui/scenic/system.h"
#include "lib/escher/escher.h"

namespace scenic {
namespace gfx {

class GfxSystem : public TempSystemDelegate {
 public:
  static constexpr TypeId kTypeId = kGfx;

  explicit GfxSystem(SystemContext context);
  ~GfxSystem();

  std::unique_ptr<CommandDispatcher> CreateCommandDispatcher(
      CommandDispatcherContext context) override;

  // TODO(MZ-452): Remove this when we externalize Displays.
  void GetDisplayInfo(ui::Scenic::GetDisplayInfoCallback callback) override;
  void TakeScreenshot(fidl::StringPtr filename,
                      ui::Scenic::TakeScreenshotCallback callback) override;
  void GetOwnershipEvent(
      ui::Scenic::GetOwnershipEventCallback callback) override;

 protected:
  // Protected so test classes can expose.
  std::unique_ptr<Engine> engine_;
  virtual std::unique_ptr<escher::Escher> InitializeEscher();
  virtual std::unique_ptr<Engine> InitializeEngine();

 private:
  void Initialize();

  DisplayManager display_manager_;

  // TODO(MZ-452): Remove this when we externalize Displays.
  void GetDisplayInfoImmediately(ui::Scenic::GetDisplayInfoCallback callback);
  void GetOwnershipEventImmediately(
      ui::Scenic::GetOwnershipEventCallback callback);

  // TODO(MZ-452): Remove this when we externalize Displays.
  bool initialized_ = false;
  std::vector<fxl::Closure> run_after_initialized_;

  escher::VulkanInstancePtr vulkan_instance_;
  escher::VulkanDeviceQueuesPtr vulkan_device_queues_;
  vk::SurfaceKHR surface_;
  std::unique_ptr<escher::Escher> escher_;
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_GFX_SYSTEM_H_
