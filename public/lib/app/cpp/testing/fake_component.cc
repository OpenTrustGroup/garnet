// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/testing/fake_component.h"

namespace fuchsia {
namespace sys {
namespace testing {

FakeComponent::FakeComponent()
    : directory_vfs_(async_get_default()),
      directory_(fbl::AdoptRef(new fs::PseudoDir())) {}

FakeComponent::~FakeComponent() = default;

void FakeComponent::Register(std::string url, FakeLauncher& fake_launcher) {
  fake_launcher.RegisterComponent(
      url, [this](fuchsia::sys::LaunchInfo launch_info,
               fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
        zx_status_t status = directory_vfs_.ServeDirectory(
            directory_, std::move(launch_info.directory_request));
        ZX_ASSERT(status == ZX_OK);
      });
}

}  // namespace testing
}  // namespace sys
}  // namespace fuchsia
