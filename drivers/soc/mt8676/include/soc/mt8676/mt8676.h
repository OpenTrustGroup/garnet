// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

typedef struct mt8675_soc {
    // SoC related
    int _unused;
} mt8675_soc_t;

zx_status_t mt8675_soc_init(mt8675_soc_t** soc);
void mt8675_soc_release(mt8675_soc_t* soc);

