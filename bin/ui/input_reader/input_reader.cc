// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/input_reader.h"

#include <fcntl.h>
#include <unistd.h>

#include <fuchsia/cpp/ui.h>
#include <lib/async/default.h>

#include "lib/fsl/tasks/message_loop.h"

namespace mozart {

constexpr char kInputDevPath[] = "/dev/class/input";

struct InputReader::DeviceInfo {
  std::unique_ptr<InputInterpreter> interpreter;
  std::unique_ptr<async::WaitMethod<InputReader,
                                    &InputReader::OnDeviceHandleReady>> waiter;
};

InputReader::InputReader(input::InputDeviceRegistry* registry,
                         bool ignore_console)
    : registry_(registry), ignore_console_(ignore_console) {
  FXL_CHECK(registry_);
}

InputReader::~InputReader() {}

void InputReader::Start() {
  device_watcher_ = fsl::DeviceWatcher::Create(
      kInputDevPath, [this](int dir_fd, std::string filename) {
        DeviceAdded(InputInterpreter::Open(dir_fd, filename, registry_));
      });
}

// Register to receive notifications that display ownership has changed
void InputReader::SetOwnershipEvent(zx::event event) {
  display_ownership_event_ = event.release();

  // Add handler to listen for signals on this event
  zx_signals_t signals = ui::displayOwnedSignal | ui::displayNotOwnedSignal;
  display_ownership_waiter_.set_object(display_ownership_event_);
  display_ownership_waiter_.set_trigger(signals);
  zx_status_t status =
      display_ownership_waiter_.Begin(async_get_default());
  FXL_CHECK(status == ZX_OK);
}

void InputReader::DeviceRemoved(zx_handle_t handle) {
  FXL_VLOG(1) << "Input device " << devices_.at(handle)->interpreter->name()
              << " removed";
  devices_.erase(handle);
}

void InputReader::DeviceAdded(std::unique_ptr<InputInterpreter> interpreter) {
  if (!interpreter)
    return;

  FXL_VLOG(1) << "Input device " << interpreter->name() << " added ";
  zx_handle_t handle = interpreter->handle();

  auto wait = std::make_unique<async::WaitMethod<InputReader,
                                    &InputReader::OnDeviceHandleReady>>(
      this, handle, ZX_USER_SIGNAL_0);

  zx_status_t status = wait->Begin(async_get_default());
  FXL_CHECK(status == ZX_OK);

  devices_.emplace(handle,
                   new DeviceInfo{std::move(interpreter), std::move(wait)});
}

void InputReader::OnDeviceHandleReady(
    async_t* async,
    async::WaitBase* wait,
    zx_status_t status,
    const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FXL_LOG(ERROR)
        << "InputReader::OnDeviceHandleReady received an error status code: "
        << status;
    return;
  }

  zx_signals_t pending = signal->observed;
  FXL_DCHECK(pending & ZX_USER_SIGNAL_0);

  auto discard = !(display_owned_ || ignore_console_);
  bool ret = devices_[wait->object()]->interpreter->Read(discard);
  if (!ret) {
    // This will destroy the waiter.
    DeviceRemoved(wait->object());
    return;
  }

  status = wait->Begin(async);
  if (status != ZX_OK) {
    FXL_LOG(ERROR)
        << "InputReader::OnDeviceHandleReady wait failed: " << status;
  }
}

void InputReader::OnDisplayHandleReady(
    async_t* async,
    async::WaitBase* wait,
    zx_status_t status,
    const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FXL_LOG(ERROR)
        << "InputReader::OnDisplayHandleReady received an error status code: "
        << status;
    return;
  }

  zx_signals_t pending = signal->observed;
  if (pending & ui::displayNotOwnedSignal) {
    display_owned_ = false;
    display_ownership_waiter_.set_trigger(ui::displayOwnedSignal);
    auto waiter_status = display_ownership_waiter_.Begin(async);
    FXL_CHECK(waiter_status == ZX_OK);
  } else if (pending & ui::displayOwnedSignal) {
    display_owned_ = true;
    display_ownership_waiter_.set_trigger(ui::displayNotOwnedSignal);
    auto waiter_status = display_ownership_waiter_.Begin(async);
    FXL_CHECK(waiter_status == ZX_OK);
  }
}

}  // namespace mozart
