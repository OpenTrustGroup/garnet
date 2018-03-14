// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/engine/session_handler.h"

#include "garnet/lib/ui/mozart/session.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/ui/scenic/fidl_helpers.h"

namespace scene_manager {

SessionHandler::SessionHandler(mz::CommandDispatcherContext dispatcher_context,
                               Engine* engine,
                               SessionId session_id,
                               mz::EventReporter* event_reporter,
                               mz::ErrorReporter* error_reporter)
    : mz::TempSessionDelegate(std::move(dispatcher_context)),
      session_manager_(engine->session_manager()),
      event_reporter_(event_reporter),
      error_reporter_(error_reporter),
      session_(::fxl::MakeRefCounted<scene_manager::Session>(
          session_id,
          engine,
          static_cast<EventReporter*>(this),
          error_reporter)) {
  FXL_DCHECK(engine);
}

SessionHandler::~SessionHandler() {
  TearDown();
}

void SessionHandler::SendEvents(::f1dl::Array<scenic::EventPtr> events) {
  auto mozart_events = ::f1dl::Array<ui_mozart::EventPtr>::New(events.size());
  for (size_t i = 0; i < events.size(); i++) {
    mozart_events[i] = ui_mozart::Event::New();
    mozart_events[i]->set_scenic(std::move(events[i]));
  }

  event_reporter_->SendEvents(std::move(mozart_events));
}

void SessionHandler::Enqueue(::f1dl::Array<ui_mozart::CommandPtr> commands) {
  // TODO: Add them all at once instead of iterating.  The problem
  // is that ::fidl::Array doesn't support this.  Or, at least reserve
  // enough space.  But ::fidl::Array doesn't support this, either.
  for (auto& command : commands) {
    FXL_CHECK(command->which() == ui_mozart::Command::Tag::SCENIC);
    buffered_ops_.push_back(std::move(command->get_scenic()));
  }
}

void SessionHandler::Present(
    uint64_t presentation_time,
    ::f1dl::Array<zx::event> acquire_fences,
    ::f1dl::Array<zx::event> release_fences,
    const ui_mozart::Session::PresentCallback& callback) {
  if (!session_->ScheduleUpdate(presentation_time, std::move(buffered_ops_),
                                std::move(acquire_fences),
                                std::move(release_fences), callback)) {
    BeginTearDown();
  }
}

void SessionHandler::HitTest(
    uint32_t node_id,
    scenic::vec3Ptr ray_origin,
    scenic::vec3Ptr ray_direction,
    const ui_mozart::Session::HitTestCallback& callback) {
  session_->HitTest(node_id, std::move(ray_origin), std::move(ray_direction),
                    callback);
}

void SessionHandler::HitTestDeviceRay(
    scenic::vec3Ptr ray_origin,
    scenic::vec3Ptr ray_direction,
    const ui_mozart::Session::HitTestDeviceRayCallback& callback) {
  session_->HitTestDeviceRay(std::move(ray_origin), std::move(ray_direction),
                             callback);
}

bool SessionHandler::ApplyCommand(const ui_mozart::CommandPtr& command) {
  // TODO(MZ-469): Implement once we push session management into Mozart.
  FXL_CHECK(false);
  return false;
}

void SessionHandler::BeginTearDown() {
  session_manager_->TearDownSession(session_->id());
  FXL_DCHECK(!session_->is_valid());
}

void SessionHandler::TearDown() {
  session_->TearDown();

  // Close the parent Mozart session.
  if (context() && context()->session()) {
    context()->mozart()->CloseSession(context()->session());
  }
}

}  // namespace scene_manager
