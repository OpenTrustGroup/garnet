// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "lib/gralloc/fidl/gralloc.fidl.h"

class GrallocImpl : public gralloc::Gralloc {
 public:
  // |gralloc::Gralloc|
  void Allocate(uint64_t size, const AllocateCallback& callback) override {
    zx::vmo vmo;
    zx_status_t status = zx::vmo::create(size, 0, &vmo);

    if (status != ZX_OK) {
      FXL_LOG(WARNING) << "Gralloc failed to allocate VMO of size: " << size
                       << " status: " << status;
    }

    callback(std::move(vmo));
  }
};

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  std::unique_ptr<app::ApplicationContext> app_context(
      app::ApplicationContext::CreateFromStartupInfo());

  GrallocImpl grallocator;

  f1dl::BindingSet<gralloc::Gralloc> bindings;

  app_context->outgoing_services()->AddService<gralloc::Gralloc>(
      [&grallocator,
       &bindings](f1dl::InterfaceRequest<gralloc::Gralloc> request) {
        bindings.AddBinding(&grallocator, std::move(request));
      });

  loop.Run();

  return 0;
}
