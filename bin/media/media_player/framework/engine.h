// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_ENGINE_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_ENGINE_H_

#include <vector>

#include "garnet/bin/media/media_player/framework/refs.h"
#include "garnet/bin/media/media_player/framework/stages/stage_impl.h"
#include "lib/fxl/functional/closure.h"

namespace media_player {

// Implements prepare, unprepare and flush.
// TODO(dalesat): This class no longer makes sense and should be refactored out.
class Engine {
 public:
  Engine();

  ~Engine();

  // Prepares the input and the subgraph upstream of it.
  void PrepareInput(Input* input);

  // Unprepares the input and the subgraph upstream of it.
  void UnprepareInput(Input* input);

  // Flushes the output and the subgraph downstream of it. |hold_frame|
  // indicates whether a video renderer should hold and display the newest
  // frame.
  void FlushOutput(Output* output, bool hold_frame);

 private:
  using UpstreamVisitor = std::function<
      void(Input* input, Output* output, StageImpl::UpstreamCallback callback)>;
  using DownstreamVisitor =
      std::function<void(Output* output,
                         Input* input,
                         StageImpl::DownstreamCallback callback)>;

  void VisitUpstream(Input* input, const UpstreamVisitor& visitor);

  void VisitDownstream(Output* output, const DownstreamVisitor& visitor);
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_ENGINE_H_
