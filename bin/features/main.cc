// Copyright 2026 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include "garnet/bin/features/feature_provider_impl.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fsl/tasks/message_loop.h"

namespace features {
namespace {

class App {
 public:
  App() : context_(component::ApplicationContext::CreateFromStartupInfo()) {
    context_->outgoing_services()->AddService<FeatureProvider>(
        [this](fidl::InterfaceRequest<FeatureProvider> request) {
          bindings_.AddBinding(&provider_, std::move(request));
        });
  }

 private:
  std::unique_ptr<component::ApplicationContext> context_;
  FeatureProviderImpl provider_;
  fidl::BindingSet<FeatureProvider> bindings_;
};

}  // namespace
}  // namespace features

int main(int argc, char** argv) {
  fsl::MessageLoop loop;
  features::App app;
  loop.Run();
  return 0;
}
