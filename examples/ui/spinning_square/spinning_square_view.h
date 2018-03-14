// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_SPINNING_SQUARE_SPINNING_SQUARE_VIEW_H_
#define GARNET_EXAMPLES_UI_SPINNING_SQUARE_SPINNING_SQUARE_VIEW_H_

#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/view_framework/base_view.h"
#include "lib/fxl/macros.h"

namespace examples {

class SpinningSquareView : public mozart::BaseView {
 public:
  SpinningSquareView(
      mozart::ViewManagerPtr view_manager,
      f1dl::InterfaceRequest<mozart::ViewOwner> view_owner_request);
  ~SpinningSquareView() override;

 private:
  // |BaseView|:
  void OnSceneInvalidated(
      ui_mozart::PresentationInfoPtr presentation_info) override;

  scenic_lib::ShapeNode background_node_;
  scenic_lib::ShapeNode square_node_;

  uint64_t start_time_ = 0u;

  FXL_DISALLOW_COPY_AND_ASSIGN(SpinningSquareView);
};

}  // namespace examples

#endif  // GARNET_EXAMPLES_UI_SPINNING_SQUARE_SPINNING_SQUARE_VIEW_H_
