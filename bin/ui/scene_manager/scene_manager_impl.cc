// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/scene_manager_impl.h"

namespace scene_manager {

SceneManagerImpl::SceneManagerImpl(std::unique_ptr<Engine> engine)
    : engine_(std::move(engine)) {}

SceneManagerImpl::~SceneManagerImpl() = default;

void SceneManagerImpl::CreateSession(
    ::fidl::InterfaceRequest<scenic::Session> request,
    ::fidl::InterfaceHandle<scenic::SessionListener> listener) {
  engine_->CreateSession(std::move(request), std::move(listener));
}

void SceneManagerImpl::GetDisplayInfo(const GetDisplayInfoCallback& callback) {
  Display* display = engine_->display_manager()->default_display();
  FXL_CHECK(display) << "There must be a default display.";

  // TODO(MZ-16): Change the terminology used in the Scenic API and its clients
  // to match the new definition of display metrics.
  auto info = scenic::DisplayInfo::New();
  info->width_in_px = display->width_in_px();
  info->height_in_px = display->height_in_px();
  callback(std::move(info));
}

}  // namespace scene_manager
