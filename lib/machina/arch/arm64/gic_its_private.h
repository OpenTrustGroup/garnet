// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "garnet/lib/machina/arch/arm64/gic_v3.h"

#define GITS_IIDR_PRODUCTID_SHIFT 24
#define PRODUCT_ID_MACHINA 0x4d /* ASCII code M */
#define IMPLEMENTER_ARM 0x43b

namespace machina {

static constexpr uint64_t kGicItsSize = 0x10000;

// clang-format off
enum class GicItsRegister : uint64_t {
  CTRL         = 0x0000,
  IIDR         = 0x0004,
  TYPER        = 0x0008,
  CBASER       = 0x0080,
  CWRITER      = 0x0088,
  CREADER      = 0x0090,
  BASER        = 0x0100,
  BASER7       = 0x0138,
  IDREGS_BASE  = 0xffd0,
  PIDR0        = 0xffe0,
  PIDR1        = 0xffe4,
  PIDR2        = 0xffe8,
  PIDR4        = 0xffd0,
  CIDR0        = 0xfff0,
  CIDR1        = 0xfff4,
  CIDR2        = 0xfff8,
  CIDR3        = 0xfffc,
};
// clang-format on

struct GicItsCmd {
  uint64_t cmd_buf[4];
};

}  // namespace machina