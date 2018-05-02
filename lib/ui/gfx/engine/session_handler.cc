// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/session_handler.h"

#include "garnet/lib/ui/scenic/session.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/ui/scenic/fidl_helpers.h"

namespace scenic {
namespace gfx {

SessionHandler::SessionHandler(CommandDispatcherContext dispatcher_context,
                               Engine* engine,
                               SessionId session_id,
                               EventReporter* event_reporter,
                               ErrorReporter* error_reporter)
    : TempSessionDelegate(std::move(dispatcher_context)),
      session_manager_(engine->session_manager()),
      event_reporter_(event_reporter),
      error_reporter_(error_reporter),
      session_(::fxl::MakeRefCounted<scenic::gfx::Session>(session_id,
                                                           engine,
                                                           event_reporter,
                                                           error_reporter)) {
  FXL_DCHECK(engine);
}

SessionHandler::~SessionHandler() {
  TearDown();
}

void SessionHandler::Present(uint64_t presentation_time,
                             ::fidl::VectorPtr<zx::event> acquire_fences,
                             ::fidl::VectorPtr<zx::event> release_fences,
                             ui::Session::PresentCallback callback) {
  if (!session_->ScheduleUpdate(
          presentation_time, std::move(buffered_commands_),
          std::move(acquire_fences), std::move(release_fences), callback)) {
    BeginTearDown();
  }
  buffered_commands_.clear();
}

void SessionHandler::HitTest(uint32_t node_id,
                             ::gfx::vec3 ray_origin,
                             ::gfx::vec3 ray_direction,
                             ui::Session::HitTestCallback callback) {
  session_->HitTest(node_id, std::move(ray_origin), std::move(ray_direction),
                    callback);
}

void SessionHandler::HitTestDeviceRay(
    ::gfx::vec3 ray_origin,
    ::gfx::vec3 ray_direction,
    ui::Session::HitTestDeviceRayCallback callback) {
  session_->HitTestDeviceRay(std::move(ray_origin), std::move(ray_direction),
                             callback);
}

void SessionHandler::DispatchCommand(ui::Command command) {
  FXL_DCHECK(command.Which() == ui::Command::Tag::kGfx);
  buffered_commands_.emplace_back(std::move(command.gfx()));
}

void SessionHandler::BeginTearDown() {
  session_manager_->TearDownSession(session_->id());
  FXL_DCHECK(!session_->is_valid());
}

void SessionHandler::TearDown() {
  session_->TearDown();

  // Close the parent Mozart session.
  if (context() && context()->session()) {
    context()->scenic()->CloseSession(context()->session());
  }
}

}  // namespace gfx
}  // namespace scenic
