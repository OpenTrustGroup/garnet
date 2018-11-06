// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <fbl/unique_fd.h>
#include <lib/async-loop/cpp/loop.h>
#include <zircon/device/trusty-vdev.h>

#include "lib/fxl/logging.h"

constexpr const char kTrustyVirtioPath[] = "/dev/misc/virtio-trusty";

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  fbl::unique_fd fd(open(kTrustyVirtioPath, O_RDWR));
  if (!fd) {
    FXL_LOG(ERROR) << "Failed to open "<< kTrustyVirtioPath;
    return ZX_ERR_IO;
  }

  zx_handle_t client_handle;
  ssize_t n = ioctl_trusty_vdev_start(fd.get(), &client_handle);
  if (n < 0) {
    FXL_LOG(ERROR) << "Failed to start trusty vdev";
    return ZX_ERR_IO;
  }

  loop.Run();
  return 0;
}
