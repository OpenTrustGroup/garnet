// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 Goldenriver Inc. All rights reserved.
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dump.h"

struct cvmd_ctl* g_cvmd_ctl = NULL;
uint8_t* g_cvmd_share = NULL;
unsigned int g_cvmd_share_size = 0;

void cvmd_init(uint64_t cvmd_shm_base, uint64_t cvmd_shm_size) {
  if (cvmd_shm_base && cvmd_shm_size >= CROSS_VM_TOTAL_SIZE) {
    char *b = (char *)cvmd_shm_base;
    g_cvmd_ctl = (struct cvmd_ctl*) (b);
    g_cvmd_share = (uint8_t*) (b + CROSS_VM_DUMP_SHARE_OFFSET);
    g_cvmd_share_size = cvmd_shm_size - CROSS_VM_DUMP_SHARE_OFFSET;
  }
}

void cvmd_deinit() {
  g_cvmd_ctl = NULL;
  g_cvmd_share = NULL;
  g_cvmd_share_size = 0;
}
