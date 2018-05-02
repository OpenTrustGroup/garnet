// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_SWAPCHAIN_DISPLAY_SWAPCHAIN_H_
#define GARNET_LIB_UI_GFX_SWAPCHAIN_DISPLAY_SWAPCHAIN_H_

#include "garnet/lib/ui/gfx/swapchain/swapchain.h"

#include <lib/zx/event.h>
#include <lib/zx/handle.h>
#include <lib/zx/vmo.h>
#include <vulkan/vulkan.hpp>

#include "garnet/lib/ui/gfx/swapchain/magma_buffer.h"
#include "garnet/lib/ui/gfx/swapchain/magma_connection.h"
#include "garnet/lib/ui/gfx/swapchain/magma_semaphore.h"
#include "garnet/lib/ui/gfx/util/event_timestamper.h"
#include "lib/escher/flib/fence_listener.h"
#include "lib/escher/resources/resource_manager.h"
#include "lib/escher/resources/resource_recycler.h"
#include "lib/escher/vk/vulkan_device_queues.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace scenic {
namespace gfx {

class Display;
class EventTimestamper;

// DisplaySwapchain implements the Swapchain interface by using a Vulkan
// swapchain to present images to a physical display using the Magma API.
class DisplaySwapchain : public Swapchain {
 public:
  DisplaySwapchain(Display* display,
                   EventTimestamper* timestamper,
                   escher::Escher* escher);
  ~DisplaySwapchain() override;

  // |Swapchain|
  bool DrawAndPresentFrame(const FrameTimingsPtr& frame_timings,
                           DrawCallback draw_callback) override;

 private:
  struct Framebuffer {
    zx::vmo vmo;
    escher::GpuMemPtr device_memory;
    escher::ImagePtr escher_image;
    MagmaBuffer magma_buffer;
  };

  struct FrameRecord {
    FrameTimingsPtr frame_timings;
    size_t swapchain_index;

    escher::SemaphorePtr render_finished_escher_semaphore;
    MagmaSemaphore render_finished_magma_semaphore;
    EventTimestamper::Watch render_finished_watch;

    MagmaSemaphore frame_presented_magma_semaphore;
    EventTimestamper::Watch frame_presented_watch;
  };
  std::unique_ptr<FrameRecord> NewFrameRecord(
      const FrameTimingsPtr& frame_timings);

  bool InitializeFramebuffers(escher::ResourceRecycler* resource_recycler);

  // When a frame is presented, the previously-presented frame becomes available
  // as a render target.
  void OnFrameRendered(size_t frame_index, zx_time_t render_finished_time);
  void OnFramePresented(size_t frame_index, zx_time_t vsync_time);

  Display* const display_;
  MagmaConnection magma_connection_;

  vk::Format format_;
  vk::Device device_;
  vk::Queue queue_;

  const escher::VulkanDeviceQueues::ProcAddrs& vulkan_proc_addresses_;

  size_t next_frame_index_ = 0;
  EventTimestamper* const timestamper_;

  std::vector<Framebuffer> swapchain_buffers_;

  std::vector<std::unique_ptr<FrameRecord>> frames_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DisplaySwapchain);
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_SWAPCHAIN_DISPLAY_SWAPCHAIN_H_
