// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/session.h"

namespace scenic {

Session::Session(Scenic* owner,
                 SessionId id,
                 ::fidl::InterfaceHandle<ui::SessionListener> listener)
    : scenic_(owner), id_(id), listener_(listener.Bind()), weak_factory_(this) {
  FXL_DCHECK(scenic_);
}

Session::~Session() = default;

void Session::Enqueue(::fidl::VectorPtr<ui::Command> cmds) {
  // TODO(MZ-469): Move Present logic into Session.
  auto& dispatcher = dispatchers_[System::TypeId::kGfx];
  FXL_DCHECK(dispatcher);
  TempSessionDelegate* delegate =
      reinterpret_cast<TempSessionDelegate*>(dispatcher.get());
  delegate->Enqueue(std::move(cmds));
}

void Session::Present(uint64_t presentation_time,
                      ::fidl::VectorPtr<zx::event> acquire_fences,
                      ::fidl::VectorPtr<zx::event> release_fences,
                      PresentCallback callback) {
  // TODO(MZ-469): Move Present logic into Session.
  auto& dispatcher = dispatchers_[System::TypeId::kGfx];
  FXL_DCHECK(dispatcher);
  TempSessionDelegate* delegate =
      reinterpret_cast<TempSessionDelegate*>(dispatcher.get());
  delegate->Present(presentation_time, std::move(acquire_fences),
                    std::move(release_fences), callback);
}

void Session::SetCommandDispatchers(
    std::array<std::unique_ptr<CommandDispatcher>, System::TypeId::kMaxSystems>
        dispatchers) {
  for (size_t i = 0; i < System::TypeId::kMaxSystems; ++i) {
    dispatchers_[i] = std::move(dispatchers[i]);
  }
}

bool Session::ApplyCommand(const ui::Command& command) {
  System::TypeId system_type = System::TypeId::kMaxSystems;  // invalid
  switch (command.Which()) {
    case ui::Command::Tag::kGfx:
      system_type = System::TypeId::kGfx;
      break;
    case ui::Command::Tag::kViews:
      system_type = System::TypeId::kViews;
      break;
    case ui::Command::Tag::kDummy:
      system_type = System::TypeId::kDummySystem;
      break;
    case ui::Command::Tag::Invalid:
      error_reporter()->ERROR() << "Session: unknown system type.";
      return false;
  }
  if (auto& dispatcher = dispatchers_[system_type]) {
    return dispatcher->ApplyCommand(command);
  } else {
    error_reporter()->ERROR()
        << "Session: no dispatcher found for system type: " << system_type;
    return false;
  }
}

void Session::HitTest(uint32_t node_id,
                      ::gfx::vec3 ray_origin,
                      ::gfx::vec3 ray_direction,
                      HitTestCallback callback) {
  auto& dispatcher = dispatchers_[System::TypeId::kGfx];
  FXL_DCHECK(dispatcher);
  TempSessionDelegate* delegate =
      reinterpret_cast<TempSessionDelegate*>(dispatcher.get());
  delegate->HitTest(node_id, std::move(ray_origin), std::move(ray_direction),
                    callback);
}

void Session::HitTestDeviceRay(::gfx::vec3 ray_origin,
                               ::gfx::vec3 ray_direction,
                               HitTestCallback callback) {
  auto& dispatcher = dispatchers_[System::TypeId::kGfx];
  FXL_DCHECK(dispatcher);
  TempSessionDelegate* delegate =
      reinterpret_cast<TempSessionDelegate*>(dispatcher.get());
  delegate->HitTestDeviceRay(std::move(ray_origin), std::move(ray_direction),
                             callback);
}

void Session::SendEvents(::fidl::VectorPtr<ui::Event> events) {
  if (listener_) {
    listener_->OnEvent(std::move(events));
  }
}

void Session::ReportError(fxl::LogSeverity severity, std::string error_string) {
  switch (severity) {
    case fxl::LOG_INFO:
      FXL_LOG(INFO) << error_string;
      break;
    case fxl::LOG_WARNING:
      FXL_LOG(WARNING) << error_string;
      break;
    case fxl::LOG_ERROR:
      FXL_LOG(ERROR) << error_string;
      if (listener_) {
        listener_->OnError(error_string);
      }
      break;
    case fxl::LOG_FATAL:
      FXL_LOG(FATAL) << error_string;
      break;
    default:
      // Invalid severity.
      FXL_DCHECK(false);
  }
}

}  // namespace scenic
