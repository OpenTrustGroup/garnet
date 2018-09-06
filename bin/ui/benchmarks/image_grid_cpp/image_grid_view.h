// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_BENCHMARKS_IMAGE_GRID_CPP_IMAGE_GRID_VIEW_H_
#define GARNET_BIN_UI_BENCHMARKS_IMAGE_GRID_CPP_IMAGE_GRID_VIEW_H_

#include "garnet/lib/ui/scenic/util/rk4_spring_simulation.h"
#include "lib/fxl/macros.h"
#include "lib/ui/view_framework/base_view.h"

class SkCanvas;

namespace image_grid {

class Frame;
class Rasterizer;

class ImageGridView : public mozart::BaseView {
 public:
  ImageGridView(fuchsia::ui::viewsv1::ViewManagerPtr view_manager,
                fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
                    view_owner_request);

  ~ImageGridView() override;

 private:
  // |BaseView|:
  void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info) override;

  void CreateScene();
  void UpdateScene(uint64_t presentation_time);

  bool scene_created_ = false;
  scenic::ShapeNode background_node_;
  scenic::EntityNode cards_parent_node_;
  std::vector<scenic::ShapeNode> cards_;

  uint64_t start_time_ = 0u;
  uint64_t last_update_time_ = 0u;
  float x_offset_ = 0.f;
  float max_scroll_offset_ = 0.f;
  scenic_impl::RK4SpringSimulation spring_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ImageGridView);
};

}  // namespace image_grid

#endif  // GARNET_BIN_UI_BENCHMARKS_IMAGE_GRID_CPP_IMAGE_GRID_VIEW_H_
