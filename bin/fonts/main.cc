// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <lib/async-loop/cpp/loop.h>

#include "lib/app/cpp/application_context.h"
#include "garnet/bin/fonts/font_provider_impl.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"

namespace fonts {

class App {
 public:
  App() : context_(component::ApplicationContext::CreateFromStartupInfo()) {
    if (!font_provider_.LoadFonts())
      exit(ZX_ERR_UNAVAILABLE);
    context_->outgoing().AddPublicService<FontProvider>(
        [this](fidl::InterfaceRequest<FontProvider> request) {
          font_provider_.AddBinding(std::move(request));
        });
  }

 private:
  std::unique_ptr<component::ApplicationContext> context_;
  FontProviderImpl font_provider_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace fonts

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  fonts::App app;
  loop.Run();
  return 0;
}
