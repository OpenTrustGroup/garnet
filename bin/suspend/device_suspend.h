// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <thread>
#include <atomic>

#include <zircon/types.h>

class DeviceSuspend {
 public:
  static DeviceSuspend* GetInstance();
  zx_status_t Initialize();
  zx_status_t TriggerDeviceSuspend();
  zx_status_t TriggerDeviceResume();

  DeviceSuspend();
  ~DeviceSuspend();

  bool IsInitialized() const { return initialized_; }
  bool IsRunning() const { return running_; }

 private:
  void DeviceSuspendLoop();
  zx_status_t WriteToDmctl(const char* cmd);

 private:
  zx_handle_t device_suspend_handle_ = ZX_HANDLE_INVALID;
  std::thread device_suspend_thread_;
  std::atomic<bool> initialized_{false};
  std::atomic<bool> running_{false};
};
