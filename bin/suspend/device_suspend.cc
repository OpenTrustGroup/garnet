// SPDX-License-Identifier: BSD-3-Clause

#include <fcntl.h>
#include <unistd.h>

#include <zircon/syscalls.h>

#include "lib/fxl/logging.h"
#include "device_suspend.h"

DeviceSuspend::DeviceSuspend() {
  FXL_LOG(INFO) << "DeviceSuspend created";
}

DeviceSuspend::~DeviceSuspend() {
  if (!initialized_)
    return;

  running_ = false;

  if (device_suspend_handle_ != ZX_HANDLE_INVALID) {
    zx_object_signal(device_suspend_handle_, 0, ZX_USER_SIGNAL_0);
  }

  if (device_suspend_thread_.joinable()) {
    device_suspend_thread_.join();
  }

  if (device_suspend_handle_ != ZX_HANDLE_INVALID) {
    zx_handle_close(device_suspend_handle_);
    device_suspend_handle_ = ZX_HANDLE_INVALID;
  }

  initialized_ = false;
  FXL_LOG(INFO) << "DeviceSuspend destroyed";
}

DeviceSuspend* DeviceSuspend::GetInstance() {
    static DeviceSuspend instance;
    return &instance;
}

zx_status_t DeviceSuspend::Initialize() {
  if (initialized_) {
    FXL_LOG(INFO) << "DeviceSuspend already initialized";
    return ZX_OK;
  }

  zx_status_t status = zx_suspend_notifier_init(&device_suspend_handle_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to init suspend notifier: " << status;
    return status;
  }

  running_ = true;
  device_suspend_thread_ = std::thread(&DeviceSuspend::DeviceSuspendLoop, this);

  initialized_ = true;
  FXL_LOG(INFO) << "DeviceSuspend initialized successfully";
  return ZX_OK;
}

zx_status_t DeviceSuspend::WriteToDmctl(const char* cmd) {
  const char* dmctl_path = "/dev/misc/dmctl";
  size_t cmd_len = strlen(cmd);

  int fd = open(dmctl_path, O_WRONLY);
  if (fd < 0) {
    FXL_LOG(ERROR) << "Failed to open dmctl: " << fd;
    return ZX_ERR_IO;
  }

  ssize_t ret = write(fd, cmd, cmd_len);
  if (ret < 0) {
    FXL_LOG(ERROR) << "Failed to write dmctl: " << ret;
    close(fd);
    return ZX_ERR_IO;
  }

  close(fd);
  return ZX_OK;
}

zx_status_t DeviceSuspend::TriggerDeviceSuspend() {
    FXL_LOG(INFO) << "Trigger device suspend";
    return WriteToDmctl("suspend");
}

zx_status_t DeviceSuspend::TriggerDeviceResume() {
    FXL_LOG(INFO) << "Trigger device resume";
    return WriteToDmctl("resume");
}

void DeviceSuspend::DeviceSuspendLoop(void) {
  zx_status_t status;
  zx_signals_t observed = 0;

  while (running_) {
    status = zx_object_wait_one(device_suspend_handle_,
                                ZX_DEVICE_SUSPEND | ZX_DEVICE_RESUME,
                                ZX_TIME_INFINITE,
                                &observed);

    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to wait signal: " << status;
      return;
    }

    if (observed & ZX_DEVICE_SUSPEND) {
      status = TriggerDeviceSuspend();
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to trigger device suspend: " << status;
        return;
      }
      status = zx_object_signal(device_suspend_handle_, ZX_DEVICE_SUSPEND, 0);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to clear suspend signal: " << status;
        return;
      }
    }

    if (observed & ZX_DEVICE_RESUME) {
      status = TriggerDeviceResume();
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to trigger device resume: " << status;
        return;
      }
      status = zx_object_signal(device_suspend_handle_, ZX_DEVICE_RESUME, 0);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to clear resume signal: " << status;
        return;
      }
    }
  }
}
