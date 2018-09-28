// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "frame_sink.h"

#include "frame_sink_view.h"
#include "util.h"

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fit/defer.h>
#include <lib/fxl/logging.h>
#include <lib/zx/vmo.h>

#include <memory>

namespace {

// bear.mp4 says 29.97, and bear.h264 is same content.
// Other longer test files want 24.
constexpr double kDefaultFramesPerSecond = 24;

}  // namespace

std::unique_ptr<FrameSink> FrameSink::Create(
    component::StartupContext* startup_context, async::Loop* main_loop,
    double frames_per_second) {
  return std::unique_ptr<FrameSink>(
      new FrameSink(startup_context, main_loop, frames_per_second));
}

FrameSink::~FrameSink() {
  // Only after ~view_provider_app_ do we know there will be zero views_ left.
  view_provider_app_.reset();
  FXL_DCHECK(views_.empty());
}

// Must be called on main_loop_'s thread.
void FrameSink::PutFrame(
    uint32_t image_id, const zx::vmo& vmo, uint64_t vmo_offset,
    std::shared_ptr<const fuchsia::mediacodec::CodecOutputConfig> output_config,
    fit::closure on_done) {
  // This method fans out to the views_, and runs on_done async when all the
  // views_ are done with the frame.

  // The alive_views_ won't change during this method because we're on
  // main_loop_'s thread.

  frames_outstanding_++;

  auto done_runner = fit::defer([this, image_id, on_done = std::move(on_done)] {
    // To be clear, Scenic ImagePipe doesn't really "release an image_id",
    // it releases an item in the present queue, but the way this example
    // program uses the present queue, it's equivalent since there's only
    // ever at most 1 usage of any given image_id in the present queue at
    // any given time.
    FXL_VLOG(3) << "Scenic released image_id: " << image_id;
    on_done();
    frames_outstanding_--;
    CheckIfAllFramesReturned();
  });
  auto shared_done_runner =
      std::make_shared<decltype(done_runner)>(std::move(done_runner));

  const fuchsia::mediacodec::VideoUncompressedFormat& video_format =
      output_config->format_details.domain->video().uncompressed();

  zx_time_t present_time;
  if (last_requested_present_time_ == ZX_TIME_INFINITE_PAST) {
    // Tell Scenic to show the first frame around now-ish.
    present_time = zx_clock_get(ZX_CLOCK_MONOTONIC) + ZX_SEC(1);
  } else {
    present_time =
        last_requested_present_time_ + ZX_SEC(1.0 / frames_per_second_);
  }
  last_requested_present_time_ = present_time;

  FXL_VLOG(3) << "putting frame - present_time: " << present_time
              << " image_id: " << image_id;

  for (FrameSinkView* view : views_) {
    view->PutFrame(image_id, present_time, vmo, vmo_offset, video_format,
                   [shared_done_runner] {
                     // ~shared_done_runner will run on_done when shared_ptr<>
                     // refcount drops to 0
                   });
  }
}

void FrameSink::PutEndOfStreamThenWaitForFramesReturnedAsync(
    fit::closure on_frames_returned) {
  // We make a blank frame and send that in to be displayed 5 seconds after the
  // last real frame, to give us a chance to see the last frame of a short .h264
  // file.  The blank frame is necessary to get Scenic to release the last real
  // frame, and this seems cleaner than any other option I can think of at the
  // moment, like relying on any particular Scenic frame release behavior if
  // RemoveImage() is called on a frame that's still (maybe) on-screen, etc.

  constexpr double kDelayBeforeBlankFrameSeconds = 5.0;

  // If this fourcc were to change, some of the other code to compute size,
  // dimensions, and generate the frame data would need to change too.
  // Currently we rely on a new VMO starting filled with 0s, which is what we
  // want anyway for BGRA / BGRA_8.
  constexpr uint32_t kBlankFrameFourcc = make_fourcc('B', 'G', 'R', 'A');
  constexpr uint32_t kBlankFrameWidth = 1;
  constexpr uint32_t kBlankFrameHeight = 1;
  constexpr uint32_t kBlankFramePixelBytes = sizeof(uint32_t);

  constexpr uint32_t kBlankFrameBytes =
      kBlankFrameWidth * kBlankFrameHeight * kBlankFramePixelBytes;
  constexpr uint32_t kBlankFrameVmoOffset = 0;

  zx_time_t blank_frame_present_time =
      last_requested_present_time_ + ZX_SEC(kDelayBeforeBlankFrameSeconds);
  ::zx::vmo blank_frame_vmo;
  zx_status_t status = ::zx::vmo::create(kBlankFrameBytes, 0, &blank_frame_vmo);
  FXL_CHECK(status == ZX_OK)
      << "::zx::vmo::create() failed - status: " << status;

  // We intentionally change the format, including pixel format, for the blank
  // frame, becaues it's easier to generate a black frame in RGB, and because
  // there's no harm in covering Scenic's ability to switch to a frame with
  // completely different format.
  fuchsia::mediacodec::VideoUncompressedFormat blank_frame_video_format{
      .primary_width_pixels = kBlankFrameWidth,
      .primary_height_pixels = kBlankFrameHeight,
      .primary_line_stride_bytes = kBlankFramePixelBytes * kBlankFrameWidth,
      .fourcc = kBlankFrameFourcc,
      // None of the other fields matter for BGRA / BGRA_8.
  };

  for (FrameSinkView* view : views_) {
    // This frame is not necessarily ever returned, which is fine.
    view->PutFrame(kBlankFrameImageId, blank_frame_present_time,
                   blank_frame_vmo, kBlankFrameVmoOffset,
                   blank_frame_video_format, nullptr);
  }

  FXL_DCHECK(!on_frames_returned_);
  on_frames_returned_ = std::move(on_frames_returned);
  // It's unlikely that this would see all the frames returned, but possible,
  // especially if Scenic died.  Normally it'll be a similar check later on
  // after a frame comes back that'll see all the frames returned.
  CheckIfAllFramesReturned();
}

void FrameSink::AddFrameSinkView(FrameSinkView* view) { views_.insert(view); }

void FrameSink::RemoveFrameSinkView(FrameSinkView* view) { views_.erase(view); }

FrameSink::FrameSink(component::StartupContext* startup_context,
                     async::Loop* main_loop, double frames_per_second)
    : startup_context_(startup_context),
      main_loop_(main_loop),
      // IEEE 754 floating point can represent 0.0 exactly.
      frames_per_second_(frames_per_second != 0.0 ? frames_per_second
                                                  : kDefaultFramesPerSecond) {
  view_provider_app_ = std::make_unique<mozart::ViewProviderApp>(
      startup_context_, [this](mozart::ViewContext view_context) {
        return FrameSinkView::Create(
            this, main_loop_, std::move(view_context.view_manager),
            std::move(view_context.view_owner_request));
      });
}

void FrameSink::CheckIfAllFramesReturned() {
  if (on_frames_returned_ && !frames_outstanding_) {
    // Always post, because calling back on same stack as setup of the async
    // wait is a completely different thing that we can just avoid doing in the
    // first place.
    async::PostTask(main_loop_->dispatcher(),
                    [on_frames_returned = std::move(on_frames_returned_)] {
                      on_frames_returned();
                    });
    FXL_DCHECK(!on_frames_returned_);
  }
}
