// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fxl/log_level.h>
#include <lib/fxl/logging.h>

#include "fake-control-impl.h"

namespace simple_camera {

const char* kFakeVendorName = "Fake Vendor Inc.";
const char* kFakeProductName = "Fake Product";

void ColorSource::FillARGB(void* start, size_t buffer_size) {
  if (!start) {
    FXL_LOG(ERROR) << "Must pass a valid buffer pointer";
    return;
  }
  uint8_t r, g, b;
  hsv_color(frame_color_, &r, &g, &b);
  FXL_VLOG(4) << "Filling with " << (int)r << " " << (int)g << " " << (int)b;
  uint32_t color = 0xff << 24 | r << 16 | g << 8 | b;
  ZX_DEBUG_ASSERT(buffer_size % 4 == 0);
  uint32_t num_pixels = buffer_size / 4;
  uint32_t* pixels = reinterpret_cast<uint32_t*>(start);
  for (unsigned int i = 0; i < num_pixels; i++) {
    pixels[i] = color;
  }

  // Ignore if flushing the cache fails.
  zx_cache_flush(start, buffer_size,
                 ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
  frame_color_ += kFrameColorInc;
  if (frame_color_ > kMaxFrameColor) {
    frame_color_ -= kMaxFrameColor;
  }
}

void ColorSource::hsv_color(uint32_t index, uint8_t* r, uint8_t* g,
                            uint8_t* b) {
  uint8_t pos = index & 0xff;
  uint8_t neg = 0xff - (index & 0xff);
  uint8_t phase = (index >> 8) & 0x7;
  uint8_t phases[6] = {0xff, 0xff, neg, 0x00, 0x00, pos};
  *r = phases[(phase + 1) % countof(phases)];
  *g = phases[(phase + 5) % countof(phases)];
  *b = phases[(phase + 3) % countof(phases)];
}

void FakeControlImpl::OnFrameAvailable(
    const fuchsia::camera::FrameAvailableEvent& frame) {
  stream_->OnFrameAvailable(frame);
}

FakeControlImpl::FakeControlImpl(fidl::InterfaceRequest<Control> control,
                                 async_dispatcher_t* dispatcher,
                                 fit::closure on_connection_closed)
    : binding_(this, fbl::move(control), dispatcher) {
  binding_.set_error_handler(fbl::move(on_connection_closed));
}

void FakeControlImpl::PostNextCaptureTask() {
  // Set the next frame time to be start + frame_count / frames per second.
  int64_t next_frame_time = frame_to_timestamp_.Apply(frame_count_++);
  FXL_DCHECK(next_frame_time > 0) << "TimelineFunction gave negative result!";
  FXL_DCHECK(next_frame_time != media::TimelineRate::kOverflow)
      << "TimelineFunction gave negative result!";
  task_.PostForTime(async_get_default_dispatcher(), zx::time(next_frame_time));
  FXL_VLOG(4) << "FakeCameraSource: setting next frame to: " << next_frame_time
              << "   "
              << next_frame_time - (int64_t)zx_clock_get(ZX_CLOCK_MONOTONIC)
              << " nsec from now";
}

// Checks which buffer can be written to,
// writes it, then signals it ready.
// Then sleeps until next cycle.
void FakeControlImpl::ProduceFrame() {
  fuchsia::camera::FrameAvailableEvent event = {};
  // For realism, give the frame a timestamp that is kFramesOfDelay frames
  // in the past:
  event.metadata.timestamp =
      frame_to_timestamp_.Apply(frame_count_ - kFramesOfDelay);
  FXL_DCHECK(event.metadata.timestamp)
      << "TimelineFunction gave negative result!";
  FXL_DCHECK(event.metadata.timestamp != media::TimelineRate::kOverflow)
      << "TimelineFunction gave negative result!";

  zx_status_t status = buffers_.GetNewBuffer();
  if (status != ZX_OK) {
    if (status == ZX_ERR_NOT_FOUND) {
      FXL_LOG(ERROR) << "no available frames, dropping frame #" << frame_count_;
      event.frame_status = fuchsia::camera::FrameStatus::ERROR_BUFFER_FULL;
    } else {
      FXL_LOG(ERROR) << "failed to get new frame, err: " << status;
      event.frame_status = fuchsia::camera::FrameStatus::ERROR_FRAME;
    }
  } else {  // Got a buffer.  Fill it with color:

    color_source_.FillARGB(buffers_.CurrentBufferAddress(),
                           buffers_.CurrentBufferSize());

    zx_status_t status = buffers_.BufferCompleted(&event.buffer_id);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "could not release the buffer: " << status;
      event.frame_status = fuchsia::camera::FrameStatus::ERROR_FRAME;
    }
  }

  OnFrameAvailable(event);
  // Schedule next frame:
  PostNextCaptureTask();
}

void FakeControlImpl::GetFormats(uint32_t index, GetFormatsCallback callback) {
  fidl::VectorPtr<fuchsia::camera::VideoFormat> formats;

  fuchsia::camera::VideoFormat format = {
      .format =
          {
              .pixel_format = {.type =
                                   fuchsia::sysmem::PixelFormatType::BGRA32},
              .width = 640,
              .height = 480,
              // .bits_per_pixel = 4,
          },
      .rate = {.frames_per_sec_numerator = 30,
               .frames_per_sec_denominator = 1}};
  format.format.planes[0].bytes_per_row = 4 * 640;

  formats.push_back(format);
  callback(fbl::move(formats), 1, ZX_OK);
}

void FakeControlImpl::GetDeviceInfo(GetDeviceInfoCallback callback) {
  fuchsia::camera::DeviceInfo camera_device_info;
  camera_device_info.vendor_name = kFakeVendorName;
  camera_device_info.product_name = kFakeProductName;
  camera_device_info.output_capabilities =
      fuchsia::camera::CAMERA_OUTPUT_STREAM;
  camera_device_info.max_stream_count = 1;
  callback(std::move(camera_device_info));
}

void FakeControlImpl::CreateStream(
    fuchsia::sysmem::BufferCollectionInfo buffer_collection,
    fuchsia::camera::FrameRate frame_rate,
    fidl::InterfaceRequest<fuchsia::camera::Stream> stream,
    zx::eventpair stream_token) {
  rate_ = frame_rate;

  buffers_.Init(buffer_collection.vmos.data(), buffer_collection.buffer_count);

  stream_ = fbl::make_unique<FakeStreamImpl>(*this, fbl::move(stream));
  stream_token_ = fbl::move(stream_token);
  // If not triggered by the token being closed, this waiter will be cancelled
  // by the destruction of this class, so the "this" pointer will be valid as
  // long as the waiter is around.
  stream_token_waiter_ = std::make_unique<async::Wait>(
      stream_token_.get(), ZX_EVENTPAIR_PEER_CLOSED, std::bind([this]() {
        stream_->Stop();
        stream_.reset();
        stream_token_.reset();
        stream_token_waiter_.reset();
      }));

  zx_status_t status =
      stream_token_waiter_->Begin(async_get_default_dispatcher());
  // The waiter, dispatcher and token are known to be valid, so this should
  // never fail.
  FXL_CHECK(status == ZX_OK);
}

void FakeControlImpl::FakeStreamImpl::OnFrameAvailable(
    const fuchsia::camera::FrameAvailableEvent& frame) {
  binding_.events().OnFrameAvailable(frame);
}

void FakeControlImpl::FakeStreamImpl::Start() {
  // Set a timeline function to convert from framecount to monotonic time.
  // The start time is now, the start frame number is 0, and the
  // conversion function from frame to time is:
  // frames_per_sec_denominator * 1e9 * num_frames) / frames_per_sec_numerator
  owner_.frame_to_timestamp_ =
      media::TimelineFunction(zx_clock_get(ZX_CLOCK_MONOTONIC), 0,
                              owner_.rate_.frames_per_sec_denominator * 1e9,
                              owner_.rate_.frames_per_sec_numerator);

  owner_.frame_count_ = 0;

  // Set the first time at which we will generate a frame:
  owner_.PostNextCaptureTask();
}

void FakeControlImpl::FakeStreamImpl::Stop() { owner_.task_.Cancel(); }

void FakeControlImpl::FakeStreamImpl::ReleaseFrame(uint32_t buffer_index) {
  owner_.buffers_.BufferRelease(buffer_index);
}

FakeControlImpl::FakeStreamImpl::FakeStreamImpl(
    FakeControlImpl& owner,
    fidl::InterfaceRequest<fuchsia::camera::Stream> stream)
    : owner_(owner), binding_(this, fbl::move(stream)) {
  binding_.set_error_handler([this] {
    // Anything to do here?
  });
}

}  // namespace simple_camera
