// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_HELLO_POSE_BUFFER_APP_H_
#define GARNET_EXAMPLES_UI_HELLO_POSE_BUFFER_APP_H_

#include "lib/app/cpp/application_context.h"

#include "lib/fsl/tasks/message_loop.h"

#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/scenic/client/session.h"

namespace hello_pose_buffer {

class App {
 public:
  App();

 private:
  // Called asynchronously by constructor.
  void Init(scenic::DisplayInfoPtr display_info);

  // Updates and presents the scene.  Called first by Init().  Each invocation
  // schedules another call to Update() when the result of the previous
  // presentation is asynchronously received.
  void Update(uint64_t next_presentation_time);

  void CreateExampleScene(float display_width, float display_height);

  void ReleaseSessionResources();

  std::unique_ptr<app::ApplicationContext> application_context_;
  fsl::MessageLoop* loop_;
  ui_mozart::MozartPtr mozart_;

  std::unique_ptr<scenic_lib::Session> session_;
  std::unique_ptr<scenic_lib::DisplayCompositor> compositor_;
  std::unique_ptr<scenic_lib::Camera> camera_;

  zx::vmo pose_buffer_vmo_;

  // Time of the first update.  Animation of the "pane" content is based on the
  // time elapsed since this time.
  uint64_t start_time_ = 0;
};

}  // namespace hello_pose_buffer

#endif  // GARNET_EXAMPLES_UI_HELLO_POSE_BUFFER_APP_H_
