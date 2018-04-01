// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_SHAPES_SHAPES_VIEW_H_
#define GARNET_EXAMPLES_UI_SHAPES_SHAPES_VIEW_H_

#include "lib/fxl/macros.h"
#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/view_framework/base_view.h"

class SkCanvas;

namespace examples {

class ShapesView : public mozart::BaseView {
 public:
  ShapesView(views_v1::ViewManagerPtr view_manager,
             fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request);

  ~ShapesView() override;

 private:
  // |BaseView|:
  void OnSceneInvalidated(images::PresentationInfo presentation_info) override;

  scenic_lib::ShapeNode background_node_;
  scenic_lib::ShapeNode card_node_;
  scenic_lib::ShapeNode circle_node_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ShapesView);
};

}  // namespace examples

#endif  // GARNET_EXAMPLES_UI_SHAPES_SHAPES_VIEW_H_
