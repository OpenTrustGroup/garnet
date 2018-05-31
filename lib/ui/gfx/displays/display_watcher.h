// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_DISPLAYS_DISPLAY_WATCHER_H_
#define GARNET_LIB_UI_GFX_DISPLAYS_DISPLAY_WATCHER_H_

#include <memory>

#include "lib/fsl/io/device_watcher.h"
#include "lib/fxl/macros.h"
#include "lib/zx/event.h"

namespace scenic {
namespace gfx {

// Waits for a display device to be available, and returns the display
// attributes through a callback.
class DisplayWatcher {
 public:
  // Callback that accepts display metrics.
  // |metrics| may be null if the display was not successfully acquired.
  using DisplayReadyCallback =
      std::function<void(fxl::UniqueFD fd, zx::channel dc_handle)>;

  DisplayWatcher();
  ~DisplayWatcher();

  // Waits for the display to become available then invokes the callback.
  void WaitForDisplay(DisplayReadyCallback callback);

 private:
  void HandleDevice(DisplayReadyCallback callback, int dir_fd,
                    std::string filename);

  std::unique_ptr<fsl::DeviceWatcher> device_watcher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DisplayWatcher);
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_DISPLAYS_DISPLAY_WATCHER_H_
