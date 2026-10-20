// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 Goldenriver Inc. All rights reserved.
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>

#include "bitops.h"

#define FIELD_EX32(storage, reg, field) \
  extract32((storage), R_##reg##_##field##_SHIFT, R_##reg##_##field##_LENGTH)

#define FIELD_EX64(storage, reg, field) \
  extract64((storage), R_##reg##_##field##_SHIFT, R_##reg##_##field##_LENGTH)

#define FIELD_DP32(storage, reg, field, val)             \
  ({                                                     \
    struct {                                             \
      unsigned int v : R_##reg##_##field##_LENGTH;       \
    } _v = {.v = val};                                   \
    uint32_t _d;                                         \
    _d = deposit32((storage), R_##reg##_##field##_SHIFT, \
                   R_##reg##_##field##_LENGTH, _v.v);    \
    _d;                                                  \
  })

#define FIELD_DP64(storage, reg, field, val)             \
  ({                                                     \
    struct {                                             \
      uint64_t v : R_##reg##_##field##_LENGTH;           \
    } _v = {.v = val};                                   \
    uint64_t _d;                                         \
    _d = deposit64((storage), R_##reg##_##field##_SHIFT, \
                   R_##reg##_##field##_LENGTH, _v.v);    \
    _d;                                                  \
  })

#define REG32(reg, addr)     \
  enum { A_##reg = (addr) }; \
  enum { R_##reg = (addr) / 4 };

#define REG8(reg, addr)      \
  enum { A_##reg = (addr) }; \
  enum { R_##reg = (addr) };

#define REG16(reg, addr)     \
  enum { A_##reg = (addr) }; \
  enum { R_##reg = (addr) / 2 };

#define REG64(reg, addr)     \
  enum { A_##reg = (addr) }; \
  enum { R_##reg = (addr) / 8 };

#define FIELD(reg, field, shift, length)          \
  enum { R_##reg##_##field##_SHIFT = (shift) };   \
  enum { R_##reg##_##field##_LENGTH = (length) }; \
  enum { R_##reg##_##field##_MASK = MAKE_64BIT_MASK(shift, length) };