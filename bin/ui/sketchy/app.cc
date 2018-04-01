// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/app.h"

namespace sketchy_service {

App::App(escher::Escher* escher)
    : loop_(fsl::MessageLoop::GetCurrent()),
      context_(component::ApplicationContext::CreateFromStartupInfo()),
      scenic_(context_->ConnectToEnvironmentService<ui::Scenic>()),
      session_(std::make_unique<scenic_lib::Session>(scenic_.get())),
      canvas_(std::make_unique<CanvasImpl>(session_.get(), escher)) {
  context_->outgoing_services()->AddService<sketchy::Canvas>(
      [this](fidl::InterfaceRequest<sketchy::Canvas> request) {
        FXL_LOG(INFO) << "Sketchy service: accepting connection to Canvas.";
        // TODO(MZ-270): Support multiple simultaneous Canvas clients.
        bindings_.AddBinding(canvas_.get(), std::move(request));
      });
  session_->set_error_handler([this] {
    FXL_LOG(INFO) << "Sketchy service lost connection to Session.";
    loop_->QuitNow();
  });
  scenic_.set_error_handler([this] {
    FXL_LOG(INFO) << "Sketchy service lost connection to Mozart.";
    loop_->QuitNow();
  });
}

}  // namespace sketchy_service
