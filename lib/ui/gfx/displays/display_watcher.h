// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_DISPLAYS_DISPLAY_WATCHER_H_
#define GARNET_LIB_UI_GFX_DISPLAYS_DISPLAY_WATCHER_H_

#include <memory>

#include "lib/zx/event.h"
#include "lib/fsl/io/device_watcher.h"
#include "lib/fxl/macros.h"

namespace scenic {
namespace gfx {

// Waits for a display device to be available, and returns the display
// attributes through a callback.
class DisplayWatcher {
 public:
  // Callback that accepts display metrics.
  // |metrics| may be null if the display was not successfully acquired.
  using DisplayReadyCallback = std::function<void(
      uint32_t width_in_px, uint32_t height_in_px, zx::event ownership_event)>;

  DisplayWatcher();
  ~DisplayWatcher();

  // Waits for the display to become available then invokes the callback.
  void WaitForDisplay(DisplayReadyCallback callback);

 private:
  void HandleDevice(bool display, int dir_fd, std::string filename);

  std::unique_ptr<fsl::DeviceWatcher> display_watcher_;
  std::unique_ptr<fsl::DeviceWatcher> framebuffer_watcher_;

  DisplayReadyCallback callback_;
  fxl::UniqueFD display_fd_;
  fxl::UniqueFD framebuffer_fd_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DisplayWatcher);
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_DISPLAYS_DISPLAY_WATCHER_H_
