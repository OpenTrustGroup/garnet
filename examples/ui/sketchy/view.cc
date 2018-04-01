// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/sketchy/view.h"

namespace sketchy_example {

View::View(component::ApplicationContext* application_context,
           views_v1::ViewManagerPtr view_manager,
           fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request)
    : BaseView(std::move(view_manager),
               std::move(view_owner_request),
               "Sketchy Example"),
      canvas_(application_context),
      background_node_(session()),
      import_node_holder_(session()),
      import_node_(&canvas_, import_node_holder_),
      scratch_group_(&canvas_),
      stable_group_(&canvas_) {
  parent_node().AddChild(background_node_);
  scenic_lib::Material background_material(session());
  background_material.SetColor(220, 220, 220, 255);
  background_node_.SetMaterial(background_material);

  parent_node().AddChild(import_node_holder_);
  import_node_holder_.SetTranslation(0.f, 0.f, 50.f);
  import_node_.AddChild(scratch_group_);
  import_node_.AddChild(stable_group_);
}

void View::OnPropertiesChanged(views_v1::ViewProperties old_properties) {
  float width = properties().view_layout->size.width;
  float height = properties().view_layout->size.height;
  scenic_lib::Rectangle background_shape(session(), width, height);
  background_node_.SetShape(background_shape);
  background_node_.SetTranslation(width * .5f, height * .5f, .1f);
  canvas_.Present(zx_clock_get(ZX_CLOCK_MONOTONIC),
                  [](images::PresentationInfo info) {});
}

bool View::OnInputEvent(input::InputEvent event) {
  if (event.is_pointer()) {
    const auto& pointer = event.pointer();
    switch (pointer.phase) {
      case input::PointerEventPhase::DOWN: {
        auto stroke = fxl::MakeRefCounted<Stroke>(&canvas_);
        pointer_id_to_stroke_map_.insert({pointer.pointer_id, stroke});
        scratch_group_.AddStroke(*stroke);
        stroke->Begin({pointer.x, pointer.y});
        canvas_.Present(zx_clock_get(ZX_CLOCK_MONOTONIC),
                        [](images::PresentationInfo info) {});
        return true;
      }
      case input::PointerEventPhase::MOVE: {
        const auto& stroke =
            pointer_id_to_stroke_map_.find(pointer.pointer_id)->second;
        if (!stroke) {
          return false;
        }
        stroke->Extend({{pointer.x, pointer.y}});
        // TODO(MZ-269): The current stroke fitter would simply connect the
        // point if Canvas::Present() is called after extending with one point.
        canvas_.Present(zx_clock_get(ZX_CLOCK_MONOTONIC),
                        [](images::PresentationInfo info) {});
        return true;
      }
      case input::PointerEventPhase::UP: {
        auto it = pointer_id_to_stroke_map_.find(pointer.pointer_id);
        const auto& stroke = it->second;
        if (!stroke) {
          return false;
        }
        stroke->Finish();
        scratch_group_.RemoveStroke(*stroke);
        stable_group_.AddStroke(*stroke);
        pointer_id_to_stroke_map_.erase(it);
        canvas_.Present(zx_clock_get(ZX_CLOCK_MONOTONIC),
                        [](images::PresentationInfo info) {});
        return true;
      }
      default:
        break;
    }
  }

  if (event.is_keyboard()) {
    const auto& keyboard = event.keyboard();
    if (keyboard.phase == input::KeyboardEventPhase::PRESSED &&
        keyboard.hid_usage == 6 /* c */) {
      stable_group_.Clear();
      canvas_.Present(zx_clock_get(ZX_CLOCK_MONOTONIC),
                      [](images::PresentationInfo info) {});
      return true;
    }
  }

  return false;
}

}  // namespace sketchy_example
