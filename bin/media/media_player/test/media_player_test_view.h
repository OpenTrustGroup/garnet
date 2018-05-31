// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <queue>

#include <lib/async-loop/cpp/loop.h>
#include <media/cpp/fidl.h>
#include <media_player/cpp/fidl.h>

#include "garnet/bin/media/media_player/test/media_player_test_params.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fxl/macros.h"
#include "lib/media/timeline/timeline_function.h"
#include "lib/ui/view_framework/base_view.h"

namespace media_player {
namespace test {

class MediaPlayerTestView : public mozart::BaseView {
 public:
  MediaPlayerTestView(
      std::function<void(int)> quit_callback,
      ::fuchsia::ui::views_v1::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner> view_owner_request,
      component::ApplicationContext* application_context,
      const MediaPlayerTestParams& params);

  ~MediaPlayerTestView() override;

 private:
  enum class State { kPaused, kPlaying, kEnded };

  // |BaseView|:
  void OnPropertiesChanged(::fuchsia::ui::views_v1::ViewProperties old_properties) override;
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override;
  void OnChildAttached(uint32_t child_key,
                       ::fuchsia::ui::views_v1::ViewInfo child_view_info) override;
  void OnChildUnavailable(uint32_t child_key) override;
  bool OnInputEvent(fuchsia::ui::input::InputEvent event) override;

  // Updates the player to read from the specified URL.
  void SetUrl(const std::string url_as_string);

  // Perform a layout of the UI elements.
  void Layout();

  // Handles a status changed event from the player.
  void HandleStatusChanged(const media_player::MediaPlayerStatus& status);

  // Handle transition to end-of-stream.
  void OnEndOfStream();

  // Toggles between play and pause.
  void TogglePlayPause();

  // Returns progress in ns.
  int64_t progress_ns() const;

  // Returns progress in the range 0.0 to 1.0.
  float normalized_progress() const;

  // Seeks to a new position and sets |seek_interval_end_|.
  void StartNewSeekInterval();

  std::function<void(int)> quit_callback_;
  const MediaPlayerTestParams& params_;
  size_t current_url_index_ = 0;

  scenic_lib::ShapeNode background_node_;
  scenic_lib::ShapeNode progress_bar_node_;
  scenic_lib::ShapeNode progress_bar_slider_node_;
  std::unique_ptr<scenic_lib::EntityNode> video_host_node_;

  media_player::MediaPlayerPtr media_player_;
  fuchsia::math::Size video_size_;
  fuchsia::math::Size pixel_aspect_ratio_;
  State state_ = State::kPaused;
  media::TimelineFunction timeline_function_;
  media_player::MediaMetadataPtr metadata_;
  fuchsia::math::RectF content_rect_;
  fuchsia::math::RectF controls_rect_;
  bool problem_shown_ = false;
  bool was_at_end_of_stream_ = false;

  int64_t seek_interval_start_ = media::kUnspecifiedTime;
  int64_t seek_interval_end_ = media::kUnspecifiedTime;
  bool in_current_seek_interval_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(MediaPlayerTestView);
};

}  // namespace test
}  // namespace media_player
