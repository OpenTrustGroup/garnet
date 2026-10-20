// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

__BEGIN_CDECLS;
#include <ddk/protocol/platform-defs.h>

#define ROUNDUP(a, b)   (((a) + ((b)-1)) & ~((b)-1))
#define ROUNDDOWN(a, b) ((a) & ~((b)-1))
#define ALIGN(a, b) ROUNDUP(a, b)

#define MONITOR_VIRQ (956)

#define  TICK_TIME 2
#define  MAX_TIME_OUT 10
#define  MAX_SEND_DATA_SIZE 10

__END_CDECLS;