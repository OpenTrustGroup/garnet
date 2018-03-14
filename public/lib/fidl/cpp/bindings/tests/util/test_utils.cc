// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/tests/util/test_utils.h"

#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <string>

namespace f1dl {
namespace test {

bool WriteTextMessage(const zx::channel& handle,
                      const std::string& text) {
  zx_status_t rv =
      handle.write(0, text.data(), static_cast<uint32_t>(text.size()),
                      nullptr, 0u);
  return rv == ZX_OK;
}

bool ReadTextMessage(const zx::channel& handle, std::string* text) {
  zx_status_t rv;
  bool did_wait = false;

  uint32_t num_bytes = 0u;
  uint32_t num_handles = 0u;
  for (;;) {
    rv = handle.read(0, nullptr, 0, &num_bytes, nullptr, 0, &num_handles);
    if (rv == ZX_ERR_SHOULD_WAIT) {
      if (did_wait) {
        ZX_DEBUG_ASSERT(false);  // Looping endlessly!?
        return false;
      }
      rv = zx_object_wait_one(handle.get(),
                              ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                              ZX_TIME_INFINITE, nullptr);
      if (rv != ZX_OK)
        return false;
      did_wait = true;
    } else {
      ZX_DEBUG_ASSERT(!num_handles);
      break;
    }
  }

  text->resize(num_bytes);
  rv = handle.read(0, &text->at(0), num_bytes, &num_bytes,
                   nullptr, num_handles, &num_handles);
  return rv == ZX_OK;
}

bool DiscardMessage(const zx::channel& handle) {
  zx_status_t rv = handle.read(ZX_CHANNEL_READ_MAY_DISCARD,
                               nullptr, 0, nullptr, nullptr, 0, nullptr);
  return rv == ZX_OK;
}

}  // namespace test
}  // namespace f1dl
