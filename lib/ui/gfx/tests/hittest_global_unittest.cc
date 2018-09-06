// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <vector>

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>

#include <lib/zx/eventpair.h>

#include "garnet/lib/ui/gfx/displays/display_manager.h"
#include "garnet/lib/ui/gfx/engine/hit.h"
#include "garnet/lib/ui/gfx/engine/hit_tester.h"
#include "garnet/lib/ui/gfx/resources/compositor/compositor.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer_stack.h"
#include "garnet/lib/ui/gfx/tests/mocks.h"
#include "garnet/lib/ui/scenic/event_reporter.h"
#include "garnet/lib/ui/scenic/util/error_reporter.h"
#include "garnet/lib/ui/scenic/util/print_event.h"
#include "gtest/gtest.h"
#include "lib/escher/forward_declarations.h"
#include "lib/fostr/fidl/fuchsia/ui/scenic/formatting.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/logging.h"
#include "lib/gtest/test_loop_fixture.h"
#include "lib/ui/input/cpp/formatting.h"
#include "lib/ui/scenic/cpp/commands.h"


// The test setup here is sufficiently different from hittest_unittest.cc to
// merit its own file.  We access the global hit test through the compositor,
// instead of through a session.

namespace scenic_impl {
namespace gfx {
namespace test {

// Session wrapper that references a common Engine.
// TODO(SCN-888): Try reduce boilerplate for testing.
class CustomSession : public EventReporter, public ErrorReporter {
 public:
  CustomSession(SessionId id, Engine* engine) : engine_(engine) {
    FXL_CHECK(engine_);
    session_ = fxl::MakeRefCounted<SessionForTest>(
        id, engine_, /*EventReporter*/ this, /*ErrorReporter*/ this);
  }

  ~CustomSession() {
    session_->TearDown();
    session_ = nullptr;
    engine_ = nullptr;
  }

  // |EventReporter|
  void EnqueueEvent(fuchsia::ui::gfx::Event event) override {
    FXL_LOG(INFO) << event;
  }

  // |EventReporter|
  void EnqueueEvent(fuchsia::ui::input::InputEvent event) override {
    FXL_LOG(INFO) << event;
  }

  // |EventReporter|
  void EnqueueEvent(fuchsia::ui::scenic::Command unhandled) override {
    FXL_LOG(INFO) << unhandled;
  }

  // |ErrorReporter|
  void ReportError(fxl::LogSeverity severity,
                   std::string error_string) override {
    FXL_LOG(FATAL) << "Unexpected error in CustomSession: " << error_string;
  }

  void Apply(::fuchsia::ui::gfx::Command command) {
    bool result = session_->ApplyCommand(std::move(command));
    ASSERT_TRUE(result) << "Failed to apply: " << command;  // Fail fast.
  }

 private:
  Engine* engine_;  // No ownership, just reference to a common engine.
  fxl::RefPtr<SessionForTest> session_;
};

// Loop fixture provides dispatcher for Engine's EventTimestamper.
using MultiSessionHitTestTest = ::gtest::TestLoopFixture;

// A comprehensive test that sets up three independent sessions, with
// View/ViewHolder pairs, and checks if global hit testing has access to
// hittable nodes across all sessions.
TEST_F(MultiSessionHitTestTest, GlobalHits) {
  DisplayManager display_manager;
  display_manager.SetDefaultDisplayForTests(std::make_unique<Display>(
      /*id*/ 0, /*px-width*/ 8, /*px-height*/ 8));
  std::unique_ptr<Engine> engine = std::make_unique<EngineForTest>(
      &display_manager, /*release fence signaller*/ nullptr);

  // Create our eventpair tokens for View/Viewholder creation.
  zx::eventpair token_v1, token_vh1, token_v2, token_vh2;
  {
    zx_status_t status;
    status = zx::eventpair::create(/*flags*/ 0u, &token_vh1, &token_v1);
    FXL_CHECK(status == ZX_OK);

    status = zx::eventpair::create(/*flags*/ 0u, &token_vh2, &token_v2);
    FXL_CHECK(status == ZX_OK);
  }

  // Root session sets up the scene and two view holders.
  CustomSession s_r(0, engine.get());
  {
    const uint32_t kCompositorId = 1001;
    const uint32_t kLayerStackId = 1002;
    const uint32_t kLayerId = 1003;
    s_r.Apply(scenic::NewCreateCompositorCmd(kCompositorId));
    s_r.Apply(scenic::NewCreateLayerStackCmd(kLayerStackId));
    s_r.Apply(scenic::NewSetLayerStackCmd(kCompositorId, kLayerStackId));
    s_r.Apply(scenic::NewCreateLayerCmd(kLayerId));
    s_r.Apply(scenic::NewSetSizeCmd(
        kLayerId, (float[2]){/*px-width*/ 8, /*px-height*/ 8}));
    s_r.Apply(scenic::NewAddLayerCmd(kLayerStackId, kLayerId));

    const uint32_t kSceneId = 1004;  // Hit
    const uint32_t kCameraId = 1005;
    const uint32_t kRendererId = 1006;
    s_r.Apply(scenic::NewCreateSceneCmd(kSceneId));
    s_r.Apply(scenic::NewCreateCameraCmd(kCameraId, kSceneId));
    s_r.Apply(scenic::NewCreateRendererCmd(kRendererId));
    s_r.Apply(scenic::NewSetCameraCmd(kRendererId, kCameraId));
    s_r.Apply(scenic::NewSetRendererCmd(kLayerId, kRendererId));

    // TODO(SCN-885) - Adjust hit count; an EntityNode shouldn't be hit.
    const uint32_t kRootNodeId = 1007;  // Hit
    s_r.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));
    s_r.Apply(scenic::NewSetTranslationCmd(
        kRootNodeId, (float[3]){/*px-width*/ 4, /*px-height*/ 4, 0}));

    const uint32_t kViewHolder1Id = 1008;
    s_r.Apply(scenic::NewAddChildCmd(kSceneId, kRootNodeId));
    s_r.Apply(scenic::NewCreateViewHolderCmd(
        kViewHolder1Id, std::move(token_vh1), "viewholder_1"));
    s_r.Apply(scenic::NewAddChildCmd(kRootNodeId, kViewHolder1Id));

    const uint32_t kViewHolder2Id = 1009;
    s_r.Apply(scenic::NewCreateViewHolderCmd(
        kViewHolder2Id, std::move(token_vh2), "viewholder_2"));
    s_r.Apply(scenic::NewAddChildCmd(kRootNodeId, kViewHolder2Id));
  }

  // Two sessions (s_1 and s_2) create an overlapping and hittable surface.
  CustomSession s_1(1, engine.get());
  {
    const uint32_t kViewId = 2001;
    s_1.Apply(scenic::NewCreateViewCmd(kViewId, std::move(token_v1), "view_1"));

    const uint32_t kRootNodeId = 2002;  // Hit
    s_1.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));
    s_1.Apply(scenic::NewAddChildCmd(kViewId, kRootNodeId));

    const uint32_t kChildId = 2003;  // Hit
    s_1.Apply(scenic::NewCreateShapeNodeCmd(kChildId));
    s_1.Apply(scenic::NewAddChildCmd(kRootNodeId, kChildId));
    s_1.Apply(scenic::NewSetTranslationCmd(kChildId,
                                           (float[3]){0.f, 0.f, /*z*/ 2.f}));

    const uint32_t kShapeId = 2004;
    s_1.Apply(scenic::NewCreateRectangleCmd(kShapeId, /*px-width*/ 8.f,
                                            /*px-height*/ 8.f));
    s_1.Apply(scenic::NewSetShapeCmd(kChildId, kShapeId));
  }

  CustomSession s_2(2, engine.get());
  {
    const uint32_t kViewId = 3001;
    s_2.Apply(scenic::NewCreateViewCmd(kViewId, std::move(token_v2), "view_2"));

    const uint32_t kRootNodeId = 3002;  // Hit
    s_2.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));
    s_2.Apply(scenic::NewAddChildCmd(kViewId, kRootNodeId));

    const uint32_t kChildId = 3003;  // Hit
    s_2.Apply(scenic::NewCreateShapeNodeCmd(kChildId));
    s_2.Apply(scenic::NewAddChildCmd(kRootNodeId, kChildId));
    s_2.Apply(scenic::NewSetTranslationCmd(kChildId,
                                           (float[3]){0.f, 0.f, /*z*/ 3.f}));

    const uint32_t kShapeId = 3004;
    s_2.Apply(scenic::NewCreateRectangleCmd(kShapeId, /*px-width*/ 8.f,
                                            /*px-height*/ 8.f));
    s_2.Apply(scenic::NewSetShapeCmd(kChildId, kShapeId));
  }

#if 0
  FXL_LOG(INFO) << engine->DumpScenes();  // Handy debugging.
#endif

  std::vector<Hit> hits;
  {
    // Models input subsystem's access to Engine internals.
    // For simplicity, we use the first (and only) compositor and layer stack.
    Compositor* compositor = engine->GetFirstCompositor();
    ASSERT_NE(compositor, nullptr);
    LayerStackPtr layer_stack = compositor->layer_stack();
    ASSERT_NE(layer_stack.get(), nullptr);

    escher::ray4 ray;
    ray.origin = escher::vec4(0.f, 0.f, -1.f, 1.f);
    ray.direction = escher::vec4(0.f, 0.f, 1.f, 0.f);
    GlobalHitTester hit_tester;
    hits = layer_stack->HitTest(ray, &hit_tester);
  }

  // All that for this!
  EXPECT_EQ(hits.size(), 6u) << "Should see six hits across three sessions.";
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
