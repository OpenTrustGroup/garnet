// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_TEST_MEDIAPLAYER_TEST_VIEW_H_
#define GARNET_BIN_MEDIAPLAYER_TEST_MEDIAPLAYER_TEST_VIEW_H_

#include <memory>
#include <queue>

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/mediaplayer/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>

#include "garnet/bin/mediaplayer/test/mediaplayer_test_params.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/macros.h"
#include "lib/media/timeline/timeline_function.h"
#include "lib/ui/view_framework/base_view.h"

namespace media_player {
namespace test {

class MediaPlayerTestView : public mozart::BaseView {
 public:
  MediaPlayerTestView(
      fit::function<void(int)> quit_callback,
      ::fuchsia::ui::viewsv1::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
          view_owner_request,
      component::StartupContext* startup_context,
      const MediaPlayerTestParams& params);

  ~MediaPlayerTestView() override;

 private:
  enum class State { kPaused, kPlaying, kEnded };

  // |BaseView|:
  void OnPropertiesChanged(
      ::fuchsia::ui::viewsv1::ViewProperties old_properties) override;
  void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info) override;
  void OnChildAttached(
      uint32_t child_key,
      ::fuchsia::ui::viewsv1::ViewInfo child_view_info) override;
  void OnChildUnavailable(uint32_t child_key) override;
  bool OnInputEvent(fuchsia::ui::input::InputEvent event) override;

  // Updates the player to read from the specified URL.
  void SetUrl(const std::string url_as_string);

  // Perform a layout of the UI elements.
  void Layout();

  // Handles a status changed event from the player.
  void HandleStatusChanged(const fuchsia::mediaplayer::PlayerStatus& status);

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

  fit::function<void(int)> quit_callback_;
  const MediaPlayerTestParams& params_;
  size_t current_url_index_ = 0;

  scenic::ShapeNode background_node_;
  scenic::ShapeNode progress_bar_node_;
  scenic::ShapeNode progress_bar_slider_node_;
  std::unique_ptr<scenic::EntityNode> video_host_node_;

  fuchsia::mediaplayer::PlayerPtr media_player_;
  fuchsia::math::Size video_size_;
  fuchsia::math::Size pixel_aspect_ratio_;
  State state_ = State::kPaused;
  media::TimelineFunction timeline_function_;
  int64_t duration_ns_ = 0;
  fuchsia::mediaplayer::MetadataPtr metadata_;
  fuchsia::math::RectF content_rect_;
  fuchsia::math::RectF controls_rect_;
  bool problem_shown_ = false;
  bool was_at_end_of_stream_ = false;

  int64_t seek_interval_start_ = fuchsia::media::NO_TIMESTAMP;
  int64_t seek_interval_end_ = fuchsia::media::NO_TIMESTAMP;
  bool in_current_seek_interval_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(MediaPlayerTestView);
};

}  // namespace test
}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_TEST_MEDIAPLAYER_TEST_VIEW_H_
