// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(MZ-540): Re-enable tests.
/*
#include "garnet/bin/ui/scene_manager/tests/scene_manager_test.h"

#include "lib/fxl/synchronization/waitable_event.h"
#include "lib/ui/tests/test_with_message_loop.h"

namespace scenic {
namespace gfx {
namespace test {

void SceneManagerTest::SetUp() {
  display_manager_.SetDefaultDisplayForTests(
      std::make_unique<Display>(1280, 800));

  auto r = std::make_unique<ReleaseFenceSignallerForTest>(
      &command_buffer_sequencer_);
  auto engine =
      std::make_unique<EngineForTest>(&display_manager_, std::move(r));

  manager_impl_ = std::make_unique<SceneManagerImpl>(std::move(engine));

  manager_binding_ = std::make_unique<fidl::Binding<gfx::SceneManager>>(
      manager_impl_.get());

  thread_ = std::make_unique<fsl::Thread>();
  thread_->Run();

  auto interface_request = manager_.NewRequest();

  fxl::ManualResetWaitableEvent wait;
  thread_->TaskRunner()->PostTask([this, &interface_request, &wait]() {
    this->manager_binding_->Bind(std::move(interface_request));
    this->manager_binding_->set_error_handler(
        [this]() { this->manager_impl_.reset(); });
    wait.Signal();
  });
  wait.Wait();
}

void SceneManagerTest::TearDown() {
  manager_ = nullptr;
  RUN_MESSAGE_LOOP_UNTIL(manager_impl_ == nullptr);
  thread_->TaskRunner()->PostTask(
      []() { fsl::MessageLoop::GetCurrent()->QuitNow(); });
  thread_->Join();
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic

*/
