// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/views/view_system.h"

namespace mz {

ViewSystem::ViewSystem(mz::SystemContext context,
                       scene_manager::ScenicSystem* scenic_system)
    : System(std::move(context)), scenic_system_(scenic_system) {}

ViewSystem::~ViewSystem() = default;

std::unique_ptr<CommandDispatcher> ViewSystem::CreateCommandDispatcher(
    mz::CommandDispatcherContext context) {
  return std::make_unique<ViewCommandDispatcher>(std::move(context),
                                                 scenic_system_);
}

ViewCommandDispatcher::ViewCommandDispatcher(
    mz::CommandDispatcherContext context,
    scene_manager::ScenicSystem* scenic_system)
    : CommandDispatcher(std::move(context)), scenic_system_(scenic_system) {
  FXL_DCHECK(scenic_system_);
}

ViewCommandDispatcher::~ViewCommandDispatcher() = default;

bool ViewCommandDispatcher::ApplyCommand(const ui_mozart::CommandPtr& command) {
  FXL_CHECK(false) << "not implemented";
  return false;
}

}  // namespace mz
