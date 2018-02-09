// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_DISPLAYS_DISPLAY_MANAGER_H_
#define GARNET_LIB_UI_SCENIC_DISPLAYS_DISPLAY_MANAGER_H_

#include <cstdint>

#include "garnet/lib/ui/scenic/displays/display.h"
#include "garnet/lib/ui/scenic/displays/display_watcher.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"

namespace scene_manager {

// Provides support for enumerating available displays.
class DisplayManager {
 public:
  DisplayManager();
  ~DisplayManager();

  // Waits for the default display to become available then invokes the
  // callback.
  void WaitForDefaultDisplay(fxl::Closure callback);

  // Gets information about the default display.
  // May return null if there isn't one.
  Display* default_display() const { return default_display_.get(); }

  // For testing.
  void SetDefaultDisplayForTests(std::unique_ptr<Display> display) {
    default_display_ = std::move(display);
  }

 private:
  void CreateDefaultDisplay(uint32_t width_in_px, uint32_t height_in_px);

  DisplayWatcher display_watcher_;
  std::unique_ptr<Display> default_display_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DisplayManager);
};

}  // namespace scene_manager

#endif  // GARNET_LIB_UI_SCENIC_DISPLAYS_DISPLAY_MANAGER_H_
