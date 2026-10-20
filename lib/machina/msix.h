// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 Goldenriver Inc. All rights reserved.
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace machina {

typedef struct msix_ctrl {
  uint16_t table_size : 10;
  uint8_t reserved : 4;
  uint8_t mask : 1;
  uint8_t enable : 1;
} msix_ctrl_t;

typedef struct msix_cap {
  uint8_t cap_vndr;
  uint8_t cap_next;
  msix_ctrl_t msg_ctrl;
  // Table. Contains the offset and the BAR indicator (BIR)
  //   2-0:  Table BAR indicator (BIR). Can be 0 to 5.
  //   31-3: Table offset in the BAR pointed by the BIR.
  uint32_t table;
  // Pending Bit Array. Contains the offset and the BAR indicator (BIR)
  //   2-0:  PBA BAR indicator (BIR). Can be 0 to 5.
  //   31-3: PBA offset in the BAR pointed by the BIR.
  uint32_t pba;
} msix_cap_t;

}  // namespace machinal