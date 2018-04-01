// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <fuchsia/cpp/escher_demo.h>
#include "garnet/examples/escher/common/demo_harness.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fsl/tasks/message_loop.h"

class DemoHarnessFuchsia : public DemoHarness, public escher_demo::EscherDemo {
 public:
  DemoHarnessFuchsia(WindowParams window_params);

  // |DemoHarness|
  void Run(Demo* demo) override;

  // |EscherDemo|
  void HandleKeyPress(uint8_t key) override;
  void HandleTouchBegin(uint64_t touch_id, double xpos, double ypos) override;
  void HandleTouchContinue(uint64_t touch_id,
                           double xpos,
                           double ypos) override;
  void HandleTouchEnd(uint64_t touch_id, double xpos, double ypos) override;

  component::ApplicationContext* application_context() {
    return application_context_.get();
  }

 private:
  // Called by Init().
  void InitWindowSystem() override;
  vk::SurfaceKHR CreateWindowAndSurface(
      const WindowParams& window_params) override;

  // Called by Init() via CreateInstance().
  void AppendPlatformSpecificInstanceExtensionNames(
      InstanceParams* params) override;

  // Called by Shutdown().
  void ShutdownWindowSystem() override;

  void RenderFrameOrQuit();

  // DemoHarnessFuchsia can work with a pre-existing MessageLoop, and also
  // create its own if necessary.
  fsl::MessageLoop* loop_;
  std::unique_ptr<fsl::MessageLoop> owned_loop_;

  std::unique_ptr<component::ApplicationContext> application_context_;
  fidl::Binding<escher_demo::EscherDemo> escher_demo_binding_;
  std::unique_ptr<component::ServiceProviderImpl> outgoing_services_;
};
