// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/mozart/mozart.h"

#include "lib/app/cpp/application_context.h"
#include "lib/fxl/functional/make_copyable.h"

namespace mz {

Mozart::Mozart(app::ApplicationContext* app_context,
               fxl::TaskRunner* task_runner,
               Clock* clock)
    : app_context_(app_context), task_runner_(task_runner), clock_(clock) {
  FXL_DCHECK(app_context_);
  FXL_DCHECK(task_runner_);
  FXL_DCHECK(clock_);

  app_context->outgoing_services()->AddService<ui_mozart::Mozart>(
      [this](f1dl::InterfaceRequest<ui_mozart::Mozart> request) {
        FXL_VLOG(1) << "Accepting connection to Mozart";
        mozart_bindings_.AddBinding(this, std::move(request));
      });
}

Mozart::~Mozart() = default;

void Mozart::OnSystemInitialized(System* system) {
  size_t num_erased = uninitialized_systems_.erase(system);
  FXL_CHECK(num_erased == 1);

  if (uninitialized_systems_.empty()) {
    for (auto& closure : run_after_all_systems_initialized_) {
      closure();
    }
    run_after_all_systems_initialized_.clear();
  }
}

void Mozart::CloseSession(Session* session) {
  for (auto& binding : session_bindings_.bindings()) {
    // It's possible that this is called by BindingSet::CloseAndCheckForEmpty.
    // In that case, binding could be empty, so check for that.
    if (binding && binding->impl().get() == session) {
      binding->Unbind();
      return;
    }
  }
}

void Mozart::CreateSession(
    ::f1dl::InterfaceRequest<ui_mozart::Session> session_request,
    ::f1dl::InterfaceHandle<ui_mozart::SessionListener> listener) {
  if (uninitialized_systems_.empty()) {
    CreateSessionImmediately(std::move(session_request), std::move(listener));
  } else {
    run_after_all_systems_initialized_.push_back(
        fxl::MakeCopyable([this, session_request = std::move(session_request),
                           listener = std::move(listener)]() mutable {
          CreateSessionImmediately(std::move(session_request),
                                   std::move(listener));
        }));
  }
}

void Mozart::CreateSessionImmediately(
    ::f1dl::InterfaceRequest<ui_mozart::Session> session_request,
    ::f1dl::InterfaceHandle<ui_mozart::SessionListener> listener) {
  auto session =
      std::make_unique<Session>(this, next_session_id_++, std::move(listener));

  // Give each installed System an opportunity to install a CommandDispatcher in
  // the newly-created Session.
  std::array<std::unique_ptr<CommandDispatcher>, System::TypeId::kMaxSystems>
      dispatchers;
  for (size_t i = 0; i < System::TypeId::kMaxSystems; ++i) {
    if (auto& system = systems_[i]) {
      dispatchers[i] = system->CreateCommandDispatcher(
          CommandDispatcherContext(this, session.get()));
    }
  }
  session->SetCommandDispatchers(std::move(dispatchers));

  session_bindings_.AddBinding(std::move(session), std::move(session_request));
}

void Mozart::GetDisplayInfo(
    const ui_mozart::Mozart::GetDisplayInfoCallback& callback) {
  FXL_DCHECK(systems_[System::kScenic]);
  TempSystemDelegate* delegate =
      reinterpret_cast<TempSystemDelegate*>(systems_[System::kScenic].get());
  delegate->GetDisplayInfo(callback);
}

}  // namespace mz
