// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_INPUT_READER_INPUT_INTERPRETER_H_
#define GARNET_BIN_UI_INPUT_READER_INPUT_INTERPRETER_H_

#include <hid/acer12.h>
#include <lib/zx/event.h>
#include <zircon/device/input.h>
#include <zircon/types.h>

#include <array>
#include <string>
#include <vector>

#include "garnet/bin/ui/input_reader/hid_decoder.h"

#include <fuchsia/cpp/input.h>
#include <fuchsia/cpp/input.h>

namespace mozart {

// Each InputInterpreter instance observes and routes events coming in from one
// file descriptor under /dev/class/input. Each file descriptor may multiplex
// events from one or more physical devices, though typically there is a 1:1
// correspondence for input devices like keyboards and mice. Sensors are an
// atypical case, where many sensors have their events routed through one
// logical file descriptor, since they share a hardware FIFO queue.

class InputInterpreter {
 public:
  enum ReportType {
    kKeyboard,
    kMouse,
    kStylus,
    kTouchscreen,
  };

  using OnReportCallback = std::function<void(ReportType type)>;

  static std::unique_ptr<InputInterpreter>
  Open(int dirfd, std::string filename, input::InputDeviceRegistry* registry);
  ~InputInterpreter();

  bool Initialize();
  bool Read(bool discard);

  const std::string& name() const { return hid_decoder_.name(); }
  zx_handle_t handle() { return event_.get(); }

 private:
  static const uint8_t kMaxSensorCount = 16;
  static const uint8_t kNoSuchSensor = 0xFF;

  enum class TouchDeviceType {
    NONE,
    ACER12,
    PARADISEv1,
    PARADISEv2,
    SAMSUNG,
    EGALAX
  };

  enum class MouseDeviceType {
    NONE,
    BOOT,
    PARADISEv1,
    PARADISEv2,
    GAMEPAD };

  enum class SensorDeviceType {
    NONE,
    PARADISE,
  };

  InputInterpreter(std::string name,
                   int fd,
                   input::InputDeviceRegistry* registry);

  void NotifyRegistry();

  void ParseKeyboardReport(uint8_t* report, size_t len);
  void ParseMouseReport(uint8_t* report, size_t len);
  void ParseGamepadMouseReport(const HidDecoder::HidGamepadSimple* gamepad);
  bool ParseAcer12TouchscreenReport(uint8_t* report, size_t len);
  bool ParseAcer12StylusReport(uint8_t* report, size_t len);
  bool ParseSamsungTouchscreenReport(uint8_t* report, size_t len);
  template <typename ReportT>
  bool ParseParadiseTouchscreenReport(uint8_t* report, size_t len);
  bool ParseEGalaxTouchscreenReport(uint8_t* report, size_t len);
  template <typename ReportT>
  bool ParseParadiseTouchpadReport(uint8_t* report, size_t len);
  bool ParseParadiseSensorReport(uint8_t* report, size_t len);

  input::InputDeviceRegistry* registry_;

  zx::event event_;

  acer12_touch_t acer12_touch_reports_[2];

  bool has_keyboard_ = false;
  input::KeyboardDescriptorPtr keyboard_descriptor_;
  bool has_mouse_ = false;
  input::MouseDescriptorPtr mouse_descriptor_;
  bool has_stylus_ = false;
  input::StylusDescriptorPtr stylus_descriptor_;
  bool has_touchscreen_ = false;
  input::TouchscreenDescriptorPtr touchscreen_descriptor_;
  bool has_sensors_ = false;
  // Arrays are indexed by the sensor number that was assigned by Zircon.
  // Keeps track of the physical sensors multiplexed over the file descriptor.
  std::array<input::SensorDescriptorPtr, kMaxSensorCount> sensor_descriptors_;
  std::array<input::InputDevicePtr, kMaxSensorCount> sensor_devices_;

  TouchDeviceType touch_device_type_ = TouchDeviceType::NONE;
  MouseDeviceType mouse_device_type_ = MouseDeviceType::NONE;
  SensorDeviceType sensor_device_type_ = SensorDeviceType::NONE;

  // Used for converting absolute coords from paradise into relative deltas
  int32_t mouse_abs_x_ = -1;
  int32_t mouse_abs_y_ = -1;

  // Keep track of which sensor gave us a report. Index into
  // |sensor_descriptors_| and |sensor_devices_|.
  uint8_t sensor_idx_ = kNoSuchSensor;

  input::InputReportPtr keyboard_report_;
  input::InputReportPtr mouse_report_;
  input::InputReportPtr touchscreen_report_;
  input::InputReportPtr stylus_report_;
  input::InputReportPtr sensor_report_;

  input::InputDevicePtr input_device_;

  HidDecoder hid_decoder_;
};

}  // namespace mozart

#endif  // GARNET_BIN_UI_INPUT_READER_INPUT_INTERPRETER_H_
