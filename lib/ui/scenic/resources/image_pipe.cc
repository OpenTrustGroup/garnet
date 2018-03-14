// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/resources/image_pipe.h"

#include <trace/event.h>

#include "garnet/lib/ui/scenic/engine/session.h"
#include "garnet/lib/ui/scenic/resources/gpu_memory.h"
#include "garnet/lib/ui/scenic/resources/host_memory.h"
#include "lib/escher/flib/fence.h"

namespace scene_manager {

const ResourceTypeInfo ImagePipe::kTypeInfo = {
    ResourceType::kImagePipe | ResourceType::kImageBase, "ImagePipe"};

ImagePipe::ImagePipe(Session* session, scenic::ResourceId id)
    : ImageBase(session, id, ImagePipe::kTypeInfo),
      weak_ptr_factory_(this),
      images_(session->error_reporter()) {}

ImagePipe::ImagePipe(Session* session,
                     scenic::ResourceId id,
                     ::f1dl::InterfaceRequest<scenic::ImagePipe> request)
    : ImageBase(session, id, ImagePipe::kTypeInfo),
      weak_ptr_factory_(this),
      handler_(std::make_unique<ImagePipeHandler>(std::move(request), this)),
      images_(session->error_reporter()) {}

void ImagePipe::AddImage(uint32_t image_id,
                         scenic::ImageInfoPtr image_info,
                         zx::vmo vmo,
                         scenic::MemoryType memory_type,
                         uint64_t memory_offset) {
  if (image_id == 0) {
    session()->error_reporter()->ERROR()
        << "ImagePipe::AddImage: Image can not be assigned an ID of 0.";
    CloseConnectionAndCleanUp();
    return;
  }
  vk::Device device = session()->engine()->vk_device();
  MemoryPtr memory;
  switch (memory_type) {
    case scenic::MemoryType::VK_DEVICE_MEMORY:
      memory = GpuMemory::New(session(), 0u, device, std::move(vmo),
                              session()->error_reporter());
      break;
    case scenic::MemoryType::HOST_MEMORY:
      memory = HostMemory::New(session(), 0u, device, std::move(vmo),
                               session()->error_reporter());
      break;
  }
  if (!memory) {
    session()->error_reporter()->ERROR()
        << "ImagePipe::AddImage: Unable to create a memory object.";
    CloseConnectionAndCleanUp();
    return;
  }
  auto image = CreateImage(session(), memory, image_info, memory_offset,
                           session()->error_reporter());
  if (!images_.AddResource(image_id, image)) {
    // Provide an additional error message to the one generated by
    // AddResource().
    session()->error_reporter()->ERROR() << "ImagePipe::AddImage had an error.";
    CloseConnectionAndCleanUp();
    return;
  }
};

void ImagePipe::CloseConnectionAndCleanUp() {
  handler_.reset();
  is_valid_ = false;
  frames_ = {};
  images_.Clear();

  // Schedule a new frame.
  session()->engine()->ScheduleUpdate(0);
}

void ImagePipe::OnConnectionError() {
  CloseConnectionAndCleanUp();
}

ImagePtr ImagePipe::CreateImage(Session* session,
                                MemoryPtr memory,
                                const scenic::ImageInfoPtr& image_info,
                                uint64_t memory_offset,
                                mz::ErrorReporter* error_reporter) {
  return Image::New(session, 0u, memory, image_info, memory_offset,
                    error_reporter);
}

void ImagePipe::RemoveImage(uint32_t image_id) {
  if (!images_.RemoveResource(image_id)) {
    // Provide an additional error message to the one generated by
    // RemoveResource().
    session()->error_reporter()->ERROR()
        << "ImagePipe::RemoveImage had an error.";
    CloseConnectionAndCleanUp();
  }
};

void ImagePipe::PresentImage(
    uint32_t image_id,
    uint64_t presentation_time,
    ::f1dl::Array<zx::event> acquire_fences,
    ::f1dl::Array<zx::event> release_fences,
    const scenic::ImagePipe::PresentImageCallback& callback) {
  if (!frames_.empty() &&
      presentation_time < frames_.back().presentation_time) {
    session()->error_reporter()->ERROR()
        << "scene_manager::ImagePipe: Present called with out-of-order "
           "presentation time."
        << "presentation_time=" << presentation_time
        << ", last scheduled presentation time="
        << frames_.back().presentation_time;
    CloseConnectionAndCleanUp();
    return;
  }

  // Verify that image_id is valid.
  if (!images_.FindResource<Image>(image_id)) {
    // Provide an additional error message to the one generated by
    // FindResource().
    session()->error_reporter()->ERROR()
        << "ImagePipe::PresentImage had an error.";
    CloseConnectionAndCleanUp();
    return;
  }

  auto acquire_fences_listener =
      std::make_unique<escher::FenceSetListener>(std::move(acquire_fences));
  acquire_fences_listener->WaitReadyAsync(
      [weak = weak_ptr_factory_.GetWeakPtr(), presentation_time] {
        if (weak) {
          weak->session()->ScheduleImagePipeUpdate(presentation_time,
                                                   ImagePipePtr(weak.get()));
        }
      });

  frames_.push(Frame{image_id, presentation_time,
                     std::move(acquire_fences_listener),
                     std::move(release_fences), callback});
};

bool ImagePipe::Update(uint64_t presentation_time,
                       uint64_t presentation_interval) {
  TRACE_DURATION("gfx", "ImagePipe::Update", "session_id", session()->id(),
                 "id", id(), "time", presentation_time, "interval",
                 presentation_interval);

  bool present_next_image = false;
  scenic::ResourceId next_image_id = current_image_id_;
  ::f1dl::Array<zx::event> next_release_fences;

  while (!frames_.empty() &&
         frames_.front().presentation_time <= presentation_time &&
         frames_.front().acquire_fences->ready()) {
    next_image_id = frames_.front().image_id;
    if (!next_release_fences.empty()) {
      // We're skipping a frame, so we can immediately signal its release
      // fences.
      for (auto& fence : next_release_fences) {
        fence.signal(0u, escher::kFenceSignalled);
      }
    }
    next_release_fences = std::move(frames_.front().release_fences);

    auto info = ui_mozart::PresentationInfo::New();
    info->presentation_time = presentation_time;
    info->presentation_interval = presentation_interval;
    if (frames_.front().present_image_callback) {
      frames_.front().present_image_callback(std::move(info));
    }
    frames_.pop();
    present_next_image = true;
  }

  if (!present_next_image)
    return false;

  auto next_image = images_.FindResource<Image>(next_image_id);

  if (!next_image) {
    session()->error_reporter()->ERROR()
        << "ImagePipe::Update() could not find Image with ID: "
        << next_image_id;
    CloseConnectionAndCleanUp();

    // Tearing down an ImagePipe will very probably result in changes to
    // the global scene-graph.
    return true;
  }

  bool image_updated = next_image->UpdatePixels();

  if (!image_updated && next_image_id == current_image_id_) {
    // This ImagePipe did not change since the last frame was rendered.
    return false;
  }

  // We're replacing a frame with a new one, so we hand off its release
  // fence to the |ReleaseFenceSignaller|, which will signal it as soon as
  // all work previously submitted to the GPU is finished.
  if (current_release_fences_) {
    session()->engine()->release_fence_signaller()->AddCPUReleaseFences(
        std::move(current_release_fences_));
  }
  current_release_fences_ = std::move(next_release_fences);
  current_image_id_ = next_image_id;
  current_image_ = std::move(next_image);

  return true;
}

const escher::ImagePtr& ImagePipe::GetEscherImage() {
  if (current_image_) {
    return current_image_->GetEscherImage();
  }
  static const escher::ImagePtr kNullEscherImage;
  return kNullEscherImage;
}

}  // namespace scene_manager
