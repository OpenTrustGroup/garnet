// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_LINUX_H_
#define GARNET_BIN_GUEST_VMM_LINUX_H_

#include "garnet/lib/machina/dev_mem.h"
#include "garnet/lib/machina/device/phys_mem.h"
#include "garnet/lib/machina/platform_device.h"

class GuestConfig;

zx_status_t setup_linux(const GuestConfig& cfg,
                        const machina::PhysMem& phys_mem,
                        const machina::DevMem& dev_mem,
                        const std::vector<machina::PlatformDevice*>& devices,
                        uintptr_t* guest_ip, uintptr_t* boot_ptr);

#endif  // GARNET_BIN_GUEST_VMM_LINUX_H_
