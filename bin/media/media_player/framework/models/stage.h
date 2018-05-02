// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_MODELS_STAGE_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_MODELS_STAGE_H_

#include "lib/fxl/functional/closure.h"

namespace media_player {

// Host for node, from the perspective of the node.
class Stage {
 public:
  virtual ~Stage() {}

  // Posts a task to run as soon as possible. A Task posted with this method is
  // run exclusive of any other such tasks.
  virtual void PostTask(const fxl::Closure& task) = 0;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_MODELS_STAGE_H_
