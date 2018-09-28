// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_PAPER_SCENE_H_
#define GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_PAPER_SCENE_H_

#include "lib/escher/escher.h"
#include "lib/escher/paper/paper_shape_cache.h"
#include "lib/escher/shape/rounded_rect_factory.h"

#include "garnet/examples/escher/waterfall/scenes/scene.h"

// Demo scene specifically designed to exercise the new PaperRenderer components
// (e.g. PaperShapeCache and PaperRenderQueue).
class PaperScene : public Scene {
 public:
  explicit PaperScene(Demo* demo);
  ~PaperScene();

  void Init(escher::Stage* stage) override;

  escher::Model* Update(const escher::Stopwatch& stopwatch,
                        uint64_t frame_count, escher::Stage* stage,
                        escher::PaperRenderer2* renderer) override;

 private:
  struct AnimatedState {
    float cycle_duration;
    size_t cycle_count_before_pause;
    float inter_cycle_pause_duration;

    // Return an animation parameter between 0 and 1;
    float Update(float current_time_sec);

    // Private.
    float state_start_time = 0.f;  // seconds;
    bool paused = false;
  };

  struct RectState {
    AnimatedState animation;
    escher::MaterialPtr material;

    // Start and end animation positions.
    escher::vec3 pos1, pos2;

    // Start and end rounded-rect shape specs.
    escher::RoundedRectSpec spec1, spec2;
  };

  struct ClipPlaneState {
    AnimatedState animation;

    // Start and end position of a point on an oriented clip plane.
    escher::vec2 pos1, pos2;

    // Start and end direction of the normal for an oriented clip plane.
    float radians1, radians2;

    // Compute an animation parameter and return the corresponding clip plane.
    escher::plane2 Update(float current_time_sec);
  };

  std::vector<RectState> rectangles_;
  std::vector<ClipPlaneState> world_space_clip_planes_;
  std::vector<ClipPlaneState> object_space_clip_planes_;

  escher::MaterialPtr red_;
  escher::MaterialPtr bg_;
  escher::MaterialPtr color1_;
  escher::MaterialPtr color2_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PaperScene);
};

#endif  // GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_PAPER_SCENE_H_
