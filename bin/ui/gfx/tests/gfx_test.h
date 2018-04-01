// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(MZ-540): Re-enable tests.
/*
#ifndef GARNET_BIN_UI_GFX_TESTS_GFX_TEST_H_
#define GARNET_BIN_UI_GFX_TESTS_GFX_TEST_H_

#include "lib/escher/forward_declarations.h"

#include "garnet/bin/ui/scene_manager/scene_manager_impl.h"
#include "garnet/lib/ui/gfx/displays/display_manager.h"
#include "garnet/lib/ui/gfx/tests/mocks.h"
#include "lib/fsl/threading/thread.h"
#include "lib/ui/gfx/fidl/scene_manager.fidl.h"

#include "gtest/gtest.h"

namespace scenic {
namespace gfx {
namespace test {

class SceneManagerTest : public ::testing::Test {
 public:
  // ::testing::Test virtual method.
  void SetUp() override;

  // ::testing::Test virtual method.
  void TearDown() override;

  SessionPtr NewSession();

 protected:
  SceneManagerImpl* manager_impl() {
    FXL_DCHECK(manager_impl_);
    return manager_impl_.get();
  }
  Engine* engine() { return manager_impl()->engine(); }
  gfx::SceneManagerPtr manager_;
  escher::impl::CommandBufferSequencer command_buffer_sequencer_;
  DisplayManager display_manager_;
  std::unique_ptr<Display> display_;
  std::unique_ptr<fidl::Binding<gfx::SceneManager>> manager_binding_;
  std::unique_ptr<fsl::Thread> thread_;

 private:
  std::unique_ptr<SceneManagerImpl> manager_impl_;
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_BIN_UI_SCENE_MANAGER_TESTS_SCENE_MANAGER_TEST_H_ */
