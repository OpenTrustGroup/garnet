// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/view_manager/view_manager_app.h"

#include "garnet/bin/ui/view_manager/view_manager_impl.h"
#include "lib/fxl/logging.h"

namespace view_manager {

ViewManagerApp::ViewManagerApp()
    : application_context_(app::ApplicationContext::CreateFromStartupInfo()) {
  FXL_DCHECK(application_context_);

  registry_.reset(new ViewRegistry(application_context_.get()));

  application_context_->outgoing_services()->AddService<mozart::ViewManager>(
      [this](f1dl::InterfaceRequest<mozart::ViewManager> request) {
        view_manager_bindings_.AddBinding(
            std::make_unique<ViewManagerImpl>(registry_.get()),
            std::move(request));
      });
}

ViewManagerApp::~ViewManagerApp() {}

}  // namespace view_manager
