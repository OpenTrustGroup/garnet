// Copyright 2019 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "time_test.h"
#include <stdlib.h>
#include <zircon/syscalls.h>
extern zx_handle_t get_root_resource(void);

void zx_clock_adjust_test(uint32_t clock_id, int64_t offset) {
  zx_handle_t channel_in;
  zx_handle_t channel_out;
  zx_channel_create(0, &channel_in, &channel_out);
  zx_clock_adjust(channel_out, clock_id, offset);
  zx_handle_close(channel_in);
  zx_handle_close(channel_out);
}
