// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdlib.h>
#include <cmath>
#include <iostream>

#include "garnet/examples/escher/common/demo.h"
#include "garnet/examples/escher/common/demo_harness.h"
#include "garnet/examples/escher/waterfall/scenes/scene.h"

#include "lib/escher/escher.h"

#include "lib/escher/geometry/types.h"
#include "lib/escher/material/color_utils.h"
#include "lib/escher/renderer/paper_renderer.h"
#include "lib/escher/renderer/shadow_map_renderer.h"
#include "lib/escher/renderer/moment_shadow_map_renderer.h"
#include "lib/escher/scene/stage.h"
#include "lib/escher/util/stopwatch.h"
#include "lib/escher/vk/vulkan_swapchain_helper.h"
#include "lib/fxl/logging.h"

class WaterfallDemo : public Demo {
 public:
  static constexpr uint32_t kDemoWidth = 2160;
  static constexpr uint32_t kDemoHeight = 1440;

  enum ShadowMode {
    kNone,
    kSsdo,
    kShadowMap,
    kMomentShadowMap,
    kNumShadowModes,
  };

  WaterfallDemo(DemoHarness* harness, int argc, char** argv);
  virtual ~WaterfallDemo();

  bool HandleKeyPress(std::string key) override;

  void DrawFrame() override;

 private:
  void ProcessCommandLineArgs(int argc, char** argv);
  void InitializeEscherStage(const DemoHarness::WindowParams& window_params);
  void InitializeDemoScenes();

  // Toggle debug overlays.
  bool show_debug_info_ = false;

  ShadowMode shadow_mode_ = ShadowMode::kShadowMap;
  int current_scene_ = 0;
  // True if the Model objects should be binned by pipeline, false if they
  // should be rendered in their natural order.
  bool sort_by_pipeline_ = true;
  // True if SSDO should be accelerated by generating a lookup table each frame.
  bool enable_ssdo_acceleration_ = true;
  bool stop_time_ = false;
  // Profile a single frame; print out timestamps about how long each part of
  // the frame took.
  bool profile_one_frame_ = false;
  // Run an offscreen benchmark.
  bool run_offscreen_benchmark_ = false;
  // True if the direction of the light source is animating.
  bool animate_light_ = true;

  // 3 camera projection modes:
  // - orthogonal full-screen
  // - perspective where floor plane is full-screen, and parallel to screen
  // - perspective from diagonal viewpoint.
  int camera_projection_mode_ = 0;

  std::vector<std::unique_ptr<Scene>> scenes_;
  escher::PaperRendererPtr renderer_;
  escher::ShadowMapRendererPtr shadow_renderer_;
  escher::ShadowMapRendererPtr moment_shadow_renderer_;
  escher::VulkanSwapchainHelper swapchain_helper_;
  escher::Stage stage_;
  double light_azimuth_radians_ = 0.f;

  escher::Stopwatch stopwatch_;
  uint64_t frame_count_ = 0;
  uint64_t first_frame_microseconds_;
};
