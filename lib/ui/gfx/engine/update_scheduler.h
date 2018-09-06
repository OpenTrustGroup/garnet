// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_UPDATE_SCHEDULER_H_
#define GARNET_LIB_UI_GFX_ENGINE_UPDATE_SCHEDULER_H_

namespace scenic_impl {
namespace gfx {

class UpdateScheduler {
 public:
  virtual void ScheduleUpdate(uint64_t presentation_time) = 0;
  virtual ~UpdateScheduler() = default;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_UPDATE_SCHEDULER_H_
