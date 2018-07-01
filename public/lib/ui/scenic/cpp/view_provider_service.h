// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SCENIC_CPP_VIEW_PROVIDER_SERVICE_H_
#define LIB_UI_SCENIC_CPP_VIEW_PROVIDER_SERVICE_H_

#include <fuchsia/ui/app/cpp/fidl.h>
#include <functional>
#include <vector>

#include "lib/app/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/ui/scenic/cpp/base_view.h"
#include "lib/ui/scenic/cpp/session.h"
#include "lib/ui/scenic/cpp/view_factory.h"

namespace scenic {

// Publishes a view provider as an outgoing service of the application.
// The views created by the view provider are owned by it and will be destroyed
// when the view provider itself is destroyed.
//
// This is only intended to be used for simple example programs.
class ViewProviderService : private fuchsia::ui::app::ViewProvider {
 public:
  explicit ViewProviderService(fuchsia::sys::StartupContext* startup_context,
                               fuchsia::ui::scenic::Scenic* scenic,
                               ViewFactory factory);
  ~ViewProviderService();

  // |ViewProvider|
  void CreateView(
      zx::eventpair view_token,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
      fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services)
      override;

 private:
  fuchsia::sys::StartupContext* const startup_context_;
  fuchsia::ui::scenic::Scenic* const scenic_;
  ViewFactory view_factory_;

  fidl::BindingSet<ViewProvider> bindings_;
  std::vector<std::unique_ptr<BaseView>> views_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewProviderService);
};

}  // namespace scenic

#endif  // LIB_UI_SCENIC_CPP_VIEW_PROVIDER_SERVICE_H_
