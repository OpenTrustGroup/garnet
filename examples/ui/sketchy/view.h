// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_SKETCHY_VIEW_H_
#define GARNET_EXAMPLES_UI_SKETCHY_VIEW_H_

#include "lib/app/cpp/application_context.h"
#include "lib/fxl/macros.h"
#include "lib/ui/sketchy/canvas.h"
#include "lib/ui/sketchy/resources.h"
#include "lib/ui/view_framework/base_view.h"

namespace sketchy_example {

using namespace sketchy_lib;

// A view that allows user to draw strokes on the screen. Pressing 'c' would
// clear the canvas.
class View final : public mozart::BaseView {
 public:
  View(app::ApplicationContext* application_context,
       mozart::ViewManagerPtr view_manager,
       f1dl::InterfaceRequest<mozart::ViewOwner> view_owner_request);

  ~View() override = default;

  // mozart::BaseView.
  void OnPropertiesChanged(mozart::ViewPropertiesPtr old_properties) override;
  bool OnInputEvent(mozart::InputEventPtr event) override;

 private:
  Canvas canvas_;
  scenic_lib::ShapeNode background_node_;
  scenic_lib::EntityNode import_node_holder_;
  ImportNode import_node_;
  StrokeGroup scratch_group_;
  StrokeGroup stable_group_;
  std::map<uint32_t, StrokePtr> pointer_id_to_stroke_map_;

  FXL_DISALLOW_COPY_AND_ASSIGN(View);
};

}  // namespace sketchy_example

#endif  // GARNET_EXAMPLES_UI_SKETCHY_VIEW_H_
