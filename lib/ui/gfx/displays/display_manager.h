// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_DISPLAYS_DISPLAY_MANAGER_H_
#define GARNET_LIB_UI_GFX_DISPLAYS_DISPLAY_MANAGER_H_

#include <cstdint>

#include "fuchsia/display/cpp/fidl.h"
#include "garnet/lib/ui/gfx/displays/display.h"
#include "garnet/lib/ui/gfx/displays/display_watcher.h"
#include "lib/async/cpp/wait.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"

#include <zircon/pixelformat.h>
#include <zx/event.h>

namespace scenic {
namespace gfx {

// Provides support for enumerating available displays.
class DisplayManager {
 public:
  DisplayManager();
  ~DisplayManager();

  // Waits for the default display to become available then invokes the
  // callback.
  void WaitForDefaultDisplay(fxl::Closure callback);

  zx::vmo AllocateDisplayMemory(int32_t size);
  // Fetches the necessary linear stride (in px) from the display controller.
  uint32_t FetchLinearStride(uint32_t width, zx_pixel_format_t format);

  uint64_t ImportEvent(const zx::event& event);
  void ReleaseEvent(uint64_t id);

  uint64_t ImportImage(const zx::vmo& vmo, int32_t width, int32_t height,
                       zx_pixel_format_t format);
  void ReleaseImage(uint64_t id);

  // Displays |buffer| on |display|. Will wait for |render_finished_event_id|
  // to be signaled before presenting. Will signal |frame_presented_event_id|
  // when the buffer is presented. Will signal |frame_signal_event_id| when the
  // image is retired.
  //
  // fuchsia::display::invalidId can be passed for any of the event_ids if
  // there is no corresponding event to signal.
  void Flip(Display* display, uint64_t buffer,
            uint64_t render_finished_event_id,
            uint64_t frame_presented_event_id, uint64_t frame_signal_event_id);

  // Gets information about the default display.
  // May return null if there isn't one.
  Display* default_display() const { return default_display_.get(); }

  // For testing.
  void SetDefaultDisplayForTests(std::unique_ptr<Display> display) {
    default_display_ = std::move(display);
  }

 private:
  void OnAsync(async_t* async, async::WaitBase* self, zx_status_t status,
               const zx_packet_signal_t* signal);
  async::WaitMethod<DisplayManager, &DisplayManager::OnAsync> wait_{this};

  void DisplaysChanged(::fidl::VectorPtr<fuchsia::display::Info> added,
                       ::fidl::VectorPtr<uint64_t> removed);
  void ClientOwnershipChange(bool has_ownership);

  fxl::UniqueFD dc_fd_;
  fidl::SynchronousInterfacePtr<fuchsia::display::Controller>
      display_controller_;
  fidl::InterfacePtr<fuchsia::display::Controller> event_dispatcher_;
  zx_handle_t dc_channel_;  // display_controller_ owns the zx::channel

  uint64_t next_event_id_ = fuchsia::display::invalidId + 1;

  DisplayWatcher display_watcher_;
  fxl::Closure display_available_cb_;
  std::unique_ptr<Display> default_display_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DisplayManager);
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_DISPLAYS_DISPLAY_MANAGER_H_
