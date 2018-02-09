// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/socket/blocking_drain.h"

#include <zx/socket.h>
#include <vector>

#include "lib/fxl/logging.h"

namespace fsl {

bool BlockingDrainFrom(
    zx::socket source,
    const std::function<size_t(const void*, uint32_t)>& write_bytes) {
  std::vector<char> buffer(64 * 1024);
  for (;;) {
    size_t bytes_read;
    zx_status_t result =
        source.read(0, buffer.data(), buffer.size(), &bytes_read);
    if (result == ZX_OK) {
      size_t bytes_written = write_bytes(buffer.data(), bytes_read);
      if (bytes_written < bytes_read) {
        FXL_LOG(ERROR) << "write_bytes callback wrote fewer bytes ("
                       << bytes_written << ") than expected (" << bytes_read
                       << ") in BlockingDrainFrom (socket closed? out of disk "
                          "space?)";
        return false;
      }
    } else if (result == ZX_ERR_SHOULD_WAIT) {
      result = source.wait_one(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED,
                               zx::time::infinite(), nullptr);
      if (result != ZX_OK) {
        // If the socket was closed, then treat as EOF.
        return result == ZX_ERR_PEER_CLOSED;
      }
    } else if (result == ZX_ERR_PEER_CLOSED) {
      // If the socket was closed, then treat as EOF.
      return true;
    } else {
      FXL_LOG(ERROR) << "Unhandled error " << result << " in BlockingDrainFrom";
      // Some other error occurred.
      return false;
    }
  }
}

}  // namespace fsl
