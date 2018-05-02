// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <fuchsia/cpp/wlantap.h>
#include <zx/channel.h>
#include <lib/async/dispatcher.h>
#include <wlan/protocol/wlantap.h>

namespace wlan {
namespace wlantap {

zx_status_t CreatePhy(zx_device_t* wlantapctl, zx::channel user_channel,
                      std::unique_ptr<::wlantap::WlantapPhyConfig> ioctl_in, async_t* loop);

} // namespace wlantap
} // namespace wlan
