// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/developer/tiles/cpp/fidl.h>
#include <gtest/gtest.h>

#include "garnet/bin/developer/tiles/tiles.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/component/cpp/testing/startup_context_for_test.h"
#include "lib/gtest/test_loop_fixture.h"

namespace {

class FakeViewManager : public fuchsia::ui::viewsv1::ViewManager {
 public:
  FakeViewManager() : binding_(this) {}

  void Bind(fidl::InterfaceRequest<fuchsia::ui::viewsv1::ViewManager> req) {
    binding_.Bind(std::move(req));
  }

 private:
  // fuchsia::ui::viewsv1::ViewManager implementation.
  void GetScenic(
      fidl::InterfaceRequest<fuchsia::ui::scenic::Scenic> scenic) final {}
  void CreateView(
      fidl::InterfaceRequest<fuchsia::ui::viewsv1::View> view,
      fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner> view_owner,
      fidl::InterfaceHandle<fuchsia::ui::viewsv1::ViewListener> view_listener,
      zx::eventpair parent_export_token, fidl::StringPtr label) final {}
  void CreateViewTree(
      fidl::InterfaceRequest<fuchsia::ui::viewsv1::ViewTree> view_tree,
      fidl::InterfaceHandle<fuchsia::ui::viewsv1::ViewTreeListener>
          view_tree_listener,
      fidl::StringPtr label) final {}

  fidl::Binding<fuchsia::ui::viewsv1::ViewManager> binding_;
};

class TilesTest : public gtest::TestLoopFixture {
 public:
  TilesTest() : context_(component::testing::StartupContextForTest::Create()) {
    // Drop original starting context to make sure nothing depends on it.
    component::StartupContext::CreateFromStartupInfo();
  }

  void SetUp() final {
    fuchsia::ui::viewsv1::ViewManagerPtr view_manager_ptr;
    view_manager_.Bind(view_manager_ptr.NewRequest());
    fuchsia::ui::viewsv1token::ViewOwnerPtr view_owner;

    tiles_impl_ = std::make_unique<tiles::Tiles>(std::move(view_manager_ptr),
                                                 view_owner.NewRequest(),
                                                 context_.get(), 10);
    tiles_ = tiles_impl_.get();
  }

  void TearDown() final {
    tiles_ = nullptr;
    tiles_impl_.reset();
  }

  fuchsia::developer::tiles::Controller* tiles() const { return tiles_; }

 private:
  FakeViewManager view_manager_;
  std::unique_ptr<component::testing::StartupContextForTest> context_;
  std::unique_ptr<tiles::Tiles> tiles_impl_;
  fuchsia::developer::tiles::Controller* tiles_;
};

TEST_F(TilesTest, Trivial) {}

TEST_F(TilesTest, AddFromURL) {
  uint32_t key = 0;
  tiles()->AddTileFromURL("test_tile", /* allow_focus */ true, {},
                          [&key](uint32_t cb_key) {
                            EXPECT_NE(0u, cb_key) << "Key should be nonzero";
                            key = cb_key;
                          });

  ASSERT_NE(0u, key) << "Key should be nonzero";

  tiles()->ListTiles([key](::fidl::VectorPtr<uint32_t> keys,
                           ::fidl::VectorPtr<::fidl::StringPtr> urls,
                           ::fidl::VectorPtr<::fuchsia::math::SizeF> sizes,
                           ::fidl::VectorPtr<bool> focusabilities) {
    ASSERT_EQ(1u, keys->size());
    EXPECT_EQ(1u, urls->size());
    EXPECT_EQ(1u, sizes->size());
    EXPECT_EQ(1u, focusabilities->size());
    EXPECT_EQ(key, keys->at(0));
    EXPECT_EQ("test_tile", urls->at(0));
    EXPECT_EQ(true, focusabilities->at(0));
  });

  tiles()->RemoveTile(key);

  tiles()->ListTiles([](::fidl::VectorPtr<uint32_t> keys,
                        ::fidl::VectorPtr<::fidl::StringPtr> urls,
                        ::fidl::VectorPtr<::fuchsia::math::SizeF> sizes,
                        ::fidl::VectorPtr<bool> focusabilities) {
    EXPECT_EQ(0u, keys->size());
    EXPECT_EQ(0u, urls->size());
    EXPECT_EQ(0u, sizes->size());
    EXPECT_EQ(0u, focusabilities->size());
  });

  tiles()->AddTileFromURL("test_nofocus_tile", /* allow_focus */ false, {},
                          [&key](uint32_t _cb_key) {
                            // noop
                          });
  tiles()->ListTiles([key](::fidl::VectorPtr<uint32_t> keys,
                           ::fidl::VectorPtr<::fidl::StringPtr> urls,
                           ::fidl::VectorPtr<::fuchsia::math::SizeF> sizes,
                           ::fidl::VectorPtr<bool> focusabilities) {
    EXPECT_EQ(1u, keys->size());
    EXPECT_EQ(1u, urls->size());
    EXPECT_EQ(1u, sizes->size());
    EXPECT_EQ(1u, focusabilities->size());
    EXPECT_EQ(false, focusabilities->at(0));
  });
}

}  // anonymous namespace
