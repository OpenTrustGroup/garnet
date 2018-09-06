// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/hello_base_view/view.h"

#include "lib/fxl/logging.h"
#include "lib/ui/input/cpp/formatting.h"

namespace hello_base_view {
using ::fuchsia::ui::input::InputEvent;
using ::fuchsia::ui::input::KeyboardEventPhase;
using ::fuchsia::ui::input::PointerEventPhase;

ShadertoyEmbedderView::ShadertoyEmbedderView(
    component::StartupContext* startup_context, async::Loop* message_loop,
    scenic::SessionPtrAndListenerRequest session_and_listener_request,
    zx::eventpair view_token)
    : scenic::BaseView(startup_context, std::move(session_and_listener_request),
                       std::move(view_token),
                       "hello_base_view ShadertoyEmbedderView"),
      message_loop_(message_loop),
      node_(session()),
      background_(session()),
      focused_(false) {
  FXL_CHECK(message_loop_);

  view().AddChild(node_);

  node_.AddChild(background_);
  scenic::Material background_material(session());
  background_material.SetColor(30, 30, 120, 255);
  background_.SetMaterial(background_material);
}

void ShadertoyEmbedderView::LaunchShadertoyClient() {
  FXL_DCHECK(!view_holder_);

  embedded_view_info_ = LaunchAppAndCreateView("shadertoy_client");

  view_holder_ = std::make_unique<scenic::ViewHolder>(
      session(), std::move(embedded_view_info_.view_holder_token),
      "shadertoy_client for hello_base_view");

  node_.Attach(*(view_holder_.get()));
}

void ShadertoyEmbedderView::OnPropertiesChanged(
    fuchsia::ui::gfx::ViewProperties old_properties) {
  if (view_holder_) {
    view_holder_->SetViewProperties(view_properties());
  }

  InvalidateScene();
}

void ShadertoyEmbedderView::OnSceneInvalidated(
    fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size()) {
    return;
  }

  const auto size = logical_size();
  const float width = size.x;
  const float height = size.y;

  scenic::RoundedRectangle background_shape(session(), width, height, 20, 20,
                                            80, 10);
  background_.SetShape(background_shape);
  background_.SetTranslation(width / 2.f, height / 2.f, 10.f);
}

// Helper for OnInputEvent: respond to pointer events.
static scenic::Material next_color(scenic::Session* session) {
  static uint8_t red = 128, green = 128, blue = 128;
  scenic::Material material(session);
  material.SetColor(red, green, blue, 255);
  red += 16;
  green += 32;
  blue += 64;
  return material;
}

void ShadertoyEmbedderView::OnInputEvent(fuchsia::ui::input::InputEvent event) {
  switch (event.Which()) {
    case InputEvent::Tag::kFocus: {
      focused_ = event.focus().focused;
      break;
    }
    case InputEvent::Tag::kPointer: {
      const auto& pointer = event.pointer();
      switch (pointer.phase) {
        case PointerEventPhase::DOWN: {
          if (focused_) {
            background_.SetMaterial(next_color(session()));
            InvalidateScene();
          }
          break;
        }
        default:
          break;  // Ignore all other pointer phases.
      }
      break;
    }
    case InputEvent::Tag::kKeyboard: {
      const auto& key = event.keyboard();
      if (key.hid_usage == /* Esc key*/ 0x29 &&
          key.phase == KeyboardEventPhase::RELEASED) {
        async::PostTask(message_loop_->dispatcher(),
                        [this] { message_loop_->Quit(); });
      }
      break;
    }
    case InputEvent::Tag::Invalid: {
      FXL_NOTREACHED();
      break;
    }
  }
}

}  // namespace hello_base_view
