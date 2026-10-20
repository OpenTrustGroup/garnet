// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2019 GoldenRiver Technologies Co., Ltd. All rights reserved.
// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <threads.h>

#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddktl/device.h>
#include <lib/fxl/macros.h>
#include <fbl/macros.h>

namespace qemu {

class Board;
using BoardType = ddk::Device<Board, ddk::Ioctlable>;

class Board : public BoardType {
 public:
  explicit Board(zx_device_t* parent, platform_bus_protocol_t pbus)
      : BoardType(parent), pbus_(pbus) {}

  static zx_status_t Bind(void* ctx, zx_device_t* parent);

  void DdkRelease() { delete this; }

  zx_status_t DdkIoctl(uint32_t op,
                       const void* in_buf,
                       size_t in_len,
                       void* out_buf,
                       size_t out_len,
                       size_t* out_actual);

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Board);

  zx_status_t PlatformDeviceEnable(const void* in_buf, size_t in_len);

  platform_bus_protocol_t pbus_;
};

}  // namespace qemu