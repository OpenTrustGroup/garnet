// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/icu_data/icu_data_provider_impl.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"

namespace icu_data {

class App {
 public:
  App() : context_(component::ApplicationContext::CreateFromStartupInfo()) {
    if (!icu_data_.LoadData())
      exit(ZX_ERR_UNAVAILABLE);
    context_->outgoing().AddPublicService<ICUDataProvider>(
        [this](fidl::InterfaceRequest<ICUDataProvider> request) {
          icu_data_.AddBinding(std::move(request));
        });
  }

 private:
  std::unique_ptr<component::ApplicationContext> context_;
  ICUDataProviderImpl icu_data_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace icu_data

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  icu_data::App app;
  loop.Run();
  return 0;
}
