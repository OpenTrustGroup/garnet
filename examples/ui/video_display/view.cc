// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <garnet/examples/ui/video_display/view.h>

#if defined(countof)
// TODO(ZX-377): Workaround for compiler error due to Zircon defining countof()
// as a macro.  Redefines countof() using GLM_COUNTOF(), which currently
// provides a more sophisticated implementation anyway.
#undef countof
#include <glm/glm.hpp>
#define countof(X) GLM_COUNTOF(X)
#else
// No workaround required.
#include <glm/glm.hpp>
#endif
#include <lib/fxl/log_level.h>
#include <lib/fxl/logging.h>
#include <glm/gtc/type_ptr.hpp>

#include <lib/ui/scenic/fidl_helpers.h>

namespace video_display {

namespace {
constexpr uint32_t kShapeWidth = 640;
constexpr uint32_t kShapeHeight = 480;
constexpr float kDisplayHeight = 50;
constexpr float kInitialWindowXPos = 320;
constexpr float kInitialWindowYPos = 240;
}  // namespace

// When a buffer is released, signal that it is available to the writer
// In this case, that means directly write to the buffer then re-present it
void View::BufferReleased(FencedBuffer* buffer) {
  FXL_VLOG(4) << "BufferReleased " << buffer->index();
  video_source_->ReleaseFrame(buffer->vmo_offset());
}

// We allow the incoming stream to reserve a write lock on a buffer
// it is writing to.  Reserving this buffer signals that it will be the latest
// buffer to be displayed. In other words, no buffer locked after this buffer
// will be displayed before this buffer.
// If the incoming buffer already filled, the driver could just call
// IncomingBufferFilled(), which will make sure the buffer is reserved first.
zx_status_t View::ReserveIncomingBuffer(FencedBuffer* buffer,
                                        uint64_t capture_time_ns) {
  if (nullptr == buffer) {
    FXL_LOG(ERROR) << "Invalid input buffer";
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t buffer_index = buffer->index();
  FXL_VLOG(4) << "Reserving incoming Buffer " << buffer_index;

  // check that no fences are set
  if (!buffer->IsAvailable()) {
    FXL_LOG(ERROR) << "Attempting to Reserve buffer " << buffer_index
                   << " which is marked unavailable.";
    return ZX_ERR_BAD_STATE;
  }

  uint64_t pres_time = frame_scheduler_.GetPresentationTimeNs(capture_time_ns);

  zx::event acquire_fence, release_fence;
  // TODO(garratt): these are supposed to be fire and forget:
  buffer->DuplicateAcquireFence(&acquire_fence);
  buffer->DuplicateReleaseFence(&release_fence);
  fidl::VectorPtr<zx::event> acquire_fences;
  acquire_fences.push_back(std::move(acquire_fence));
  fidl::VectorPtr<zx::event> release_fences;
  release_fences.push_back(std::move(release_fence));
  FXL_VLOG(4) << "presenting Buffer " << buffer_index << " at " << pres_time;

  image_pipe_->PresentImage(
      buffer_index, pres_time, std::move(acquire_fences),
      std::move(release_fences),
      [this, pres_time](const images::PresentationInfo& info) {
        this->frame_scheduler_.OnFramePresented(
            info.presentation_time, info.presentation_interval, pres_time);
      });
  return ZX_OK;
}

// When an incoming buffer is filled, View releases the aquire fence
zx_status_t View::IncomingBufferFilled(const camera_vb_frame_notify_t& frame) {
  FencedBuffer* buffer;
  if (frame.error != 0) {
    FXL_LOG(ERROR) << "Error set on incoming frame. Error: " << frame.error;
    return ZX_OK;  // no reason to stop the channel...
  }

  zx_status_t status = FindOrCreateBuffer(
      frame.frame_size, frame.data_vb_offset, &buffer, format_);
  if (ZX_OK != status) {
    FXL_LOG(ERROR) << "Failed to create a frame for the incoming buffer";
    // What can we do here? If we cannot display the frame, quality will
    // suffer...
    return status;
  }

  // Now we know that the buffer exists.
  // If we have not reserved the buffer, do so now. ReserveIncomingBuffer
  // will quietly return if the buffer is already reserved.
  if (frame.metadata.timestamp <= 0) {
    FXL_LOG(ERROR) << "Frame has bad timestamp: " << frame.metadata.timestamp;
    return ZX_ERR_OUT_OF_RANGE;
  }
  uint64_t capture_time_ns = frame.metadata.timestamp;
  status = ReserveIncomingBuffer(buffer, capture_time_ns);
  if (ZX_OK != status) {
    FXL_LOG(ERROR) << "Failed to reserve a frame for the incoming buffer";
    return status;
  }

  // Signal that the buffer is ready to be presented:
  buffer->Signal();

  return ZX_OK;
}

// This is a stand-in for some actual gralloc type service which would allocate
// the right type of memory for the application and return it as a vmo.
zx_status_t Gralloc(uint64_t buffer_size, uint32_t num_buffers, zx::vmo* buffer_vmo) {
  // In the future, some special alignment might happen here, or special
  // memory allocated...
  return zx::vmo::create(num_buffers * buffer_size, 0, buffer_vmo);
}

// This function is a stand-in for the fact that our formats are not
// standardized accross the platform.  This is an issue, we are tracking
// it as (MTWN-98).
images::PixelFormat ConvertFormat(camera_pixel_format_t driver_format) {
  switch (driver_format) {
    case RGB32:
      return images::PixelFormat::BGRA_8;
    case YUY2:
      return images::PixelFormat::YUY2;
    default:
      FXL_DCHECK(false) << "Unsupported format!";
  }
  return images::PixelFormat::BGRA_8;
}

zx_status_t View::FindOrCreateBuffer(uint32_t frame_size,
                                     uint64_t vmo_offset,
                                     FencedBuffer** buffer,
                                     const camera_video_format_t& format) {
  if (buffer != nullptr) {
    *buffer = nullptr;
  }
  // If the buffer exists, return the pointer
  for (std::unique_ptr<FencedBuffer>& b : frame_buffers_) {
    // TODO(garratt): For some cameras, the frame size changes.  Debug this
    // in the UVC driver.
    if (b->vmo_offset() == vmo_offset && b->size() >= frame_size) {
      if (nullptr != buffer) {
        *buffer = b.get();
      }
      return ZX_OK;
    }
  }
  // Buffer does not exist, make a new one!
  last_buffer_index_++;
  FXL_VLOG(4) << "Creating buffer " << last_buffer_index_;
  // TODO(garratt): change back to frame_size when we fix the fact that they are
  // changing...
  std::unique_ptr<FencedBuffer> b = FencedBuffer::Create(
      max_frame_size_, vmo_, vmo_offset, last_buffer_index_);
  if (b == nullptr) {
    return ZX_ERR_INTERNAL;
  }
  // Set release fence callback so we know when a frame is made available
  b->SetReleaseFenceHandler(
      [this](FencedBuffer* b) { this->BufferReleased(b); });
  b->Reset();
  if (buffer != nullptr) {
    *buffer = b.get();
  }

  // Now add that buffer to the image pipe:
  FXL_VLOG(4) << "Creating ImageInfo ";
  // auto image_info = images::ImageInfo::New();
  images::ImageInfo image_info;
  image_info.stride = format.stride;
  image_info.tiling = images::Tiling::LINEAR;
  image_info.width = format.width;
  image_info.height = format.height;

  // To make things look like a webcam application, mirror left-right.
  image_info.transform = images::Transform::FLIP_HORIZONTAL;

  zx::vmo vmo;
  zx_status_t status = b->DuplicateVmoWithoutWrite(&vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to duplicate VMO from buffer";
    return status;
  }

  image_info.pixel_format = ConvertFormat(format.pixel_format);
  image_pipe_->AddImage(b->index(), image_info, std::move(vmo),
                        images::MemoryType::HOST_MEMORY, vmo_offset);

  frame_buffers_.push_back(std::move(b));
  return ZX_OK;
}

View::View(component::ApplicationContext* application_context,
           views_v1::ViewManagerPtr view_manager,
           fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request,
           bool use_fake_camera)
    : BaseView(std::move(view_manager),
               std::move(view_owner_request),
               "Video Display Example"),
      loop_(fsl::MessageLoop::GetCurrent()),
      node_(session()) {
  FXL_VLOG(4) << "Creating View";
  // Create an ImagePipe and pass one end to the Session:
  uint32_t image_pipe_id = session()->AllocResourceId();
  session()->Enqueue(scenic_lib::NewCreateImagePipeCommand(
      image_pipe_id, image_pipe_.NewRequest()));

  // Create a material that has our image pipe mapped onto it:
  scenic_lib::Material material(session());
  material.SetTexture(image_pipe_id);
  session()->ReleaseResource(image_pipe_id);

  // Create a rounded-rect shape to display the camera image on.
  scenic_lib::RoundedRectangle shape(session(), kShapeWidth, kShapeHeight, 80,
                                     80, 80, 80);

  node_.SetShape(shape);
  node_.SetMaterial(material);
  parent_node().AddChild(node_);
  // Translation of 0, 0 is the middle of the screen
  node_.SetTranslation(kInitialWindowXPos, kInitialWindowYPos, kDisplayHeight);
  InvalidateScene();

  FXL_VLOG(4) << "Creating View - set up image pipe";
  if (use_fake_camera) {
    video_source_ = std::make_unique<FakeCameraSource>();
  } else {
    video_source_ = std::make_unique<CameraClient>();
  }

  zx_status_t open_status = video_source_->Open(0);
  if (open_status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to open the camera. Quitting!";
    // TODO(garratt): This does not actually quit.
    loop_->QuitNow();
    return;
  }
  zx_status_t status = video_source_->GetSupportedFormats(
      fbl::BindMember(this, &View::OnGetFormats));
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get Supported Formats. Quitting!";
    loop_->QuitNow();
    return;
  }
}

// Asyncronous setup of camera:
// 1) Get format
// 2) Set format
// 3) Set buffer
// 4) Start

zx_status_t View::OnGetFormats(
    const std::vector<camera_video_format_t>& out_formats) {
  // For now, just configure to the first format available:
  if (out_formats.size() < 1) {
    FXL_LOG(ERROR) << "No supported formats available";
    return ZX_ERR_INTERNAL;
  }
  // For other configurations, we would chose a format in a fancier way...
  format_ = out_formats[0];
  FXL_VLOG(4) << "Chose format.  Capture Type: " << format_.capture_type
              << " W:H:S = " << format_.width << ":" << format_.height << ":"
              << format_.stride << " bbp: " << format_.bits_per_pixel
              << " format: " << format_.pixel_format;
  return video_source_->SetFormat(format_,
                                  fbl::BindMember(this, &View::OnSetFormat));
}

zx_status_t View::OnSetFormat(uint64_t max_frame_size) {
  FXL_VLOG(4) << "OnSetFormat: max_frame_size: " << max_frame_size
              << "  making buffer size: " << max_frame_size * kNumberOfBuffers;
  // Allocate the memory:

  if (max_frame_size < format_.stride * format_.height) {
    FXL_VLOG(4) << "OnSetFormat: max_frame_size: " << max_frame_size
                << " < needed frame size: " << format_.stride * format_.height;
    max_frame_size = format_.stride * format_.height;
  }
  max_frame_size_ = max_frame_size;
  zx_status_t status = Gralloc(max_frame_size, kNumberOfBuffers, &vmo_);
  if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to allocate memory for video stream!";
      return status;
  }

  // Tell the driver about the memory:
  status = video_source_->SetBuffer(vmo_);
  if (status != ZX_OK) {
    return status;
  }
  return video_source_->Start(
      fbl::BindMember(this, &View::IncomingBufferFilled));
}

View::~View() = default;

void View::OnSceneInvalidated(images::PresentationInfo presentation_info) {
  if (!has_logical_size()) {
    return;
  }

  // Compute the amount of time that has elapsed since the view was created.
  double seconds =
      static_cast<double>(presentation_info.presentation_time) / 1'000'000'000;

  const float kHalfWidth = logical_size().width * 0.5f;
  const float kHalfHeight = logical_size().height * 0.5f;

  // Compute the translation for the window to swirl around the screen.
  // Why do this?  Well, this is an example of what a View can do, and it helps
  // debug the camera to know if scenic is still running.
  node_.SetTranslation(kHalfWidth * (1. + .1 * sin(seconds * 0.8)),
                       kHalfHeight * (1. + .1 * sin(seconds * 0.6)),
                       kDisplayHeight);

  // The rounded-rectangles are constantly animating; invoke InvalidateScene()
  // to guarantee that OnSceneInvalidated() will be called again.
  InvalidateScene();
}

// This function is also for debugging.
// TODO(garratt): This never gets called.
bool View::OnInputEvent(input::InputEvent event) {
  if (event.is_keyboard()) {
    const auto& keyboard = event.keyboard();
    if (keyboard.phase == input::KeyboardEventPhase::PRESSED) {
      FXL_LOG(INFO) << "Key Pressed = " << keyboard.hid_usage;
    }
    return true;
  }
  return false;
}

}  // namespace video_display
