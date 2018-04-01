// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/input/device_state.h"

#include <fuchsia/cpp/input.h>
#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/fxl/time/time_point.h"

namespace {
int64_t InputEventTimestampNow() {
  return fxl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
}
}  // namespace

namespace mozart {

constexpr fxl::TimeDelta kKeyRepeatSlow = fxl::TimeDelta::FromMilliseconds(250);
constexpr fxl::TimeDelta kKeyRepeatFast = fxl::TimeDelta::FromMilliseconds(75);

#pragma mark - KeyboardState

KeyboardState::KeyboardState(DeviceState* device_state)
    : device_state_(device_state),
      keymap_(qwerty_map),
      weak_ptr_factory_(this),
      task_runner_(fsl::MessageLoop::GetCurrent()->task_runner()) {
  char* keys = getenv("gfxconsole.keymap");
  if (keys && !strcmp(keys, "dvorak")) {
    keymap_ = dvorak_map;
  }
}

void KeyboardState::SendEvent(input::KeyboardEventPhase phase,
                              uint32_t key,
                              uint64_t modifiers,
                              uint64_t timestamp) {
  input::InputEvent ev;
  input::KeyboardEvent kb;
  kb.phase = phase;
  kb.event_time = timestamp;
  kb.device_id = device_state_->device_id();
  kb.hid_usage = key;
  kb.code_point = hid_map_key(
      key, modifiers & (input::kModifierShift | input::kModifierCapsLock),
      keymap_);
  kb.modifiers = modifiers;
  ev.set_keyboard(std::move(kb));
  device_state_->callback()(std::move(ev));
}

void KeyboardState::Update(input::InputReport input_report) {
  FXL_DCHECK(input_report.keyboard);

  uint64_t now = input_report.event_time;
  std::vector<uint32_t> old_keys = keys_;
  keys_.clear();
  repeat_keys_.clear();

  for (uint32_t key : *input_report.keyboard->pressed_keys) {
    keys_.push_back(key);
    auto it = std::find(old_keys.begin(), old_keys.end(), key);
    if (it != old_keys.end()) {
      old_keys.erase(it);
      continue;
    }

    SendEvent(input::KeyboardEventPhase::PRESSED, key, modifiers_, now);

    uint64_t modifiers = modifiers_;
    switch (key) {
      case HID_USAGE_KEY_LEFT_SHIFT:
        modifiers_ |= input::kModifierLeftShift;
        break;
      case HID_USAGE_KEY_RIGHT_SHIFT:
        modifiers_ |= input::kModifierRightShift;
        break;
      case HID_USAGE_KEY_LEFT_CTRL:
        modifiers_ |= input::kModifierLeftControl;
        break;
      case HID_USAGE_KEY_RIGHT_CTRL:
        modifiers_ |= input::kModifierRightControl;
        break;
      case HID_USAGE_KEY_LEFT_ALT:
        modifiers_ |= input::kModifierLeftAlt;
        break;
      case HID_USAGE_KEY_RIGHT_ALT:
        modifiers_ |= input::kModifierRightAlt;
        break;
      case HID_USAGE_KEY_LEFT_GUI:
        modifiers_ |= input::kModifierLeftSuper;
        break;
      case HID_USAGE_KEY_RIGHT_GUI:
        modifiers_ |= input::kModifierRightSuper;
        break;
      default:
        break;
    }

    // Don't repeat modifier by themselves
    if (modifiers == modifiers_) {
      repeat_keys_.push_back(key);
    }
  }

  // If any key was released as well, do not repeat
  if (!old_keys.empty()) {
    repeat_keys_.clear();
  }

  for (uint32_t key : old_keys) {
    SendEvent(input::KeyboardEventPhase::RELEASED, key, modifiers_, now);

    switch (key) {
      case HID_USAGE_KEY_LEFT_SHIFT:
        modifiers_ &= (~input::kModifierLeftShift);
        break;
      case HID_USAGE_KEY_RIGHT_SHIFT:
        modifiers_ &= (~input::kModifierRightShift);
        break;
      case HID_USAGE_KEY_LEFT_CTRL:
        modifiers_ &= (~input::kModifierLeftControl);
        break;
      case HID_USAGE_KEY_RIGHT_CTRL:
        modifiers_ &= (~input::kModifierRightControl);
        break;
      case HID_USAGE_KEY_LEFT_ALT:
        modifiers_ &= (~input::kModifierLeftAlt);
        break;
      case HID_USAGE_KEY_RIGHT_ALT:
        modifiers_ &= (~input::kModifierRightAlt);
        break;
      case HID_USAGE_KEY_LEFT_GUI:
        modifiers_ &= (~input::kModifierLeftSuper);
        break;
      case HID_USAGE_KEY_RIGHT_GUI:
        modifiers_ &= (~input::kModifierRightSuper);
        break;
      case HID_USAGE_KEY_CAPSLOCK:
        if (modifiers_ & input::kModifierCapsLock) {
          modifiers_ &= (~input::kModifierCapsLock);
        } else {
          modifiers_ |= input::kModifierCapsLock;
        }
        break;
      default:
        break;
    }
  }

  if (!repeat_keys_.empty()) {
    ScheduleRepeat(++repeat_sequence_, kKeyRepeatSlow);
  } else {
    ++repeat_sequence_;
  }
}

void KeyboardState::Repeat(uint64_t sequence) {
  if (sequence != repeat_sequence_) {
    return;
  }
  uint64_t now = InputEventTimestampNow();
  for (uint32_t key : repeat_keys_) {
    SendEvent(input::KeyboardEventPhase::REPEAT, key, modifiers_, now);
  }
  ScheduleRepeat(sequence, kKeyRepeatFast);
}

void KeyboardState::ScheduleRepeat(uint64_t sequence, fxl::TimeDelta delta) {
  task_runner_->PostDelayedTask(
      [ weak = weak_ptr_factory_.GetWeakPtr(), sequence ] {
        if (weak)
          weak->Repeat(sequence);
      },
      delta);
}

#pragma mark - MouseState

void MouseState::OnRegistered() {}

void MouseState::OnUnregistered() {}

void MouseState::SendEvent(float rel_x,
                           float rel_y,
                           int64_t timestamp,
                           input::PointerEventPhase phase,
                           uint32_t buttons) {
  input::InputEvent ev;
  input::PointerEvent pt;
  pt.event_time = timestamp;
  pt.device_id = device_state_->device_id();
  pt.pointer_id = device_state_->device_id();
  pt.phase = phase;
  pt.buttons = buttons;
  pt.type = input::PointerEventType::MOUSE;
  pt.x = rel_x;
  pt.y = rel_y;
  ev.set_pointer(std::move(pt));
  device_state_->callback()(std::move(ev));
}

void MouseState::Update(input::InputReport input_report,
                        geometry::Size display_size) {
  FXL_DCHECK(input_report.mouse);
  uint64_t now = input_report.event_time;
  uint8_t pressed = (input_report.mouse->pressed_buttons ^ buttons_) &
                    input_report.mouse->pressed_buttons;
  uint8_t released =
      (input_report.mouse->pressed_buttons ^ buttons_) & buttons_;
  buttons_ = input_report.mouse->pressed_buttons;

  // TODO(jpoichet) Update once we have an API to capture mouse.
  // TODO(MZ-385): Quantize the mouse value to the range [0, display_width -
  // mouse_resolution]
  position_.x =
      std::max(0.0f, std::min(position_.x + input_report.mouse->rel_x,
                              static_cast<float>(display_size.width)));
  position_.y =
      std::max(0.0f, std::min(position_.y + input_report.mouse->rel_y,
                              static_cast<float>(display_size.height)));

  if (!pressed && !released) {
    SendEvent(position_.x, position_.y, now, input::PointerEventPhase::MOVE,
              buttons_);
  } else {
    if (pressed) {
      SendEvent(position_.x, position_.y, now,
                input::PointerEventPhase::DOWN, pressed);
    }
    if (released) {
      SendEvent(position_.x, position_.y, now, input::PointerEventPhase::UP,
                released);
    }
  }
}

#pragma mark - StylusState

void StylusState::SendEvent(int64_t timestamp,
                            input::PointerEventPhase phase,
                            input::PointerEventType type,
                            float x,
                            float y,
                            uint32_t buttons) {
  input::PointerEvent pt;
  pt.event_time = timestamp;
  pt.device_id = device_state_->device_id();
  pt.pointer_id = 1;
  pt.type = type;
  pt.phase = phase;
  pt.x = x;
  pt.y = y;
  pt.buttons = buttons;

  stylus_ = pt;

  input::InputEvent ev;
  ev.set_pointer(std::move(pt));
  device_state_->callback()(std::move(ev));
}

void StylusState::Update(input::InputReport input_report,
                         geometry::Size display_size) {
  FXL_DCHECK(input_report.stylus);

  input::StylusDescriptor* descriptor = device_state_->stylus_descriptor();
  FXL_DCHECK(descriptor);

  const bool previous_stylus_down = stylus_down_;
  const bool previous_stylus_in_range = stylus_in_range_;
  stylus_down_ = input_report.stylus->is_in_contact;
  stylus_in_range_ = input_report.stylus->in_range;

  input::PointerEventPhase phase = input::PointerEventPhase::DOWN;
  if (stylus_down_) {
    if (previous_stylus_down) {
      phase = input::PointerEventPhase::MOVE;
    }
  } else {
    if (previous_stylus_down) {
      phase = input::PointerEventPhase::UP;
    } else {
      if (stylus_in_range_ && !previous_stylus_in_range) {
        inverted_stylus_ = input_report.stylus->is_inverted;
        phase = input::PointerEventPhase::ADD;
      } else if (!stylus_in_range_ && previous_stylus_in_range) {
        phase = input::PointerEventPhase::REMOVE;
      } else if (stylus_in_range_) {
        phase = input::PointerEventPhase::HOVER;
      } else {
        return;
      }
    }
  }

  uint64_t now = input_report.event_time;

  if (phase == input::PointerEventPhase::UP) {
    SendEvent(now, phase,
              inverted_stylus_ ? input::PointerEventType::INVERTED_STYLUS
                               : input::PointerEventType::STYLUS,
              stylus_.x, stylus_.y, stylus_.buttons);
  } else {
    // Quantize the value to [0, 1) based on the resolution.
    float x_denominator =
        (1 + static_cast<float>(descriptor->x.range.max -
                                descriptor->x.range.min) /
                 static_cast<float>(descriptor->x.resolution)) *
        static_cast<float>(descriptor->x.resolution);
    float x =
        static_cast<float>(display_size.width * (input_report.stylus->x -
                                                 descriptor->x.range.min)) /
        x_denominator;

    float y_denominator =
        (1 + static_cast<float>(descriptor->y.range.max -
                                descriptor->y.range.min) /
                 static_cast<float>(descriptor->y.resolution)) *
        static_cast<float>(descriptor->y.resolution);
    float y =
        static_cast<float>(display_size.height * (input_report.stylus->y -
                                                  descriptor->y.range.min)) /
        y_denominator;

    uint32_t buttons = 0;
    if (input_report.stylus->pressed_buttons & input::kStylusBarrel) {
      buttons |= input::kStylusPrimaryButton;
    }
    SendEvent(now, phase,
              inverted_stylus_ ? input::PointerEventType::INVERTED_STYLUS
                               : input::PointerEventType::STYLUS,
              x, y, buttons);
  }
}

#pragma mark - TouchscreenState

void TouchscreenState::Update(input::InputReport input_report,
                              geometry::Size display_size) {
  FXL_DCHECK(input_report.touchscreen);
  input::TouchscreenDescriptor* descriptor =
      device_state_->touchscreen_descriptor();
  FXL_DCHECK(descriptor);

  std::vector<input::PointerEvent> old_pointers = pointers_;
  pointers_.clear();

  uint64_t now = input_report.event_time;

  for (auto& touch : *input_report.touchscreen->touches) {
    input::InputEvent ev;
    input::PointerEvent pt;
    pt.event_time = now;
    pt.device_id = device_state_->device_id();
    pt.phase = input::PointerEventPhase::DOWN;
    for (auto it = old_pointers.begin(); it != old_pointers.end(); ++it) {
      FXL_DCHECK(touch.finger_id >= 0);
      if (it->pointer_id == static_cast<uint32_t>(touch.finger_id)) {
        pt.phase = input::PointerEventPhase::MOVE;
        old_pointers.erase(it);
        break;
      }
    }

    pt.pointer_id = touch.finger_id;
    pt.type = input::PointerEventType::TOUCH;

    // Quantize the value to [0, 1) based on the resolution.
    float x_denominator =
        (1 + static_cast<float>(descriptor->x.range.max -
                                descriptor->x.range.min) /
                 static_cast<float>(descriptor->x.resolution)) *
        static_cast<float>(descriptor->x.resolution);
    float x = static_cast<float>(display_size.width *
                                 (touch.x - descriptor->x.range.min)) /
              x_denominator;

    float y_denominator =
        (1 + static_cast<float>(descriptor->y.range.max -
                                descriptor->y.range.min) /
                 static_cast<float>(descriptor->y.resolution)) *
        static_cast<float>(descriptor->y.resolution);
    float y = static_cast<float>(display_size.height *
                                 (touch.y - descriptor->y.range.min)) /
              y_denominator;

    uint32_t width = 2 * touch.width;
    uint32_t height = 2 * touch.height;

    pt.x = x;
    pt.y = y;
    pt.radius_major = width > height ? width : height;
    pt.radius_minor = width > height ? height : width;
    pointers_.push_back(pt);

    // For now when we get DOWN we need to fake trigger ADD first.
    if (pt.phase == input::PointerEventPhase::DOWN) {
      input::InputEvent add_ev;
      zx_status_t clone_result = fidl::Clone(ev, &add_ev);
      FXL_DCHECK(clone_result);
      input::PointerEvent add_pt;
      clone_result = fidl::Clone(pt, &add_pt);
      FXL_DCHECK(clone_result);
      add_pt.phase = input::PointerEventPhase::ADD;
      add_ev.set_pointer(std::move(add_pt));
      device_state_->callback()(std::move(add_ev));
    }

    ev.set_pointer(std::move(pt));
    device_state_->callback()(std::move(ev));
  }

  for (const auto& pointer : old_pointers) {
    input::InputEvent ev;
    input::PointerEvent pt;
    zx_status_t clone_result = fidl::Clone(pointer, &pt);
    FXL_DCHECK(clone_result);
    pt.phase = input::PointerEventPhase::UP;
    pt.event_time = now;
    ev.set_pointer(std::move(pt));
    device_state_->callback()(std::move(ev));

    ev = input::InputEvent();
    clone_result = fidl::Clone(pointer, &pt);
    FXL_DCHECK(clone_result);
    pt.phase = input::PointerEventPhase::REMOVE;
    pt.event_time = now;
    ev.set_pointer(std::move(pt));
    device_state_->callback()(std::move(ev));
  }
}

void SensorState::Update(input::InputReport input_report) {
  FXL_DCHECK(input_report.sensor);
  FXL_DCHECK(device_state_->sensor_descriptor());
}

#pragma mark - DeviceState

DeviceState::DeviceState(uint32_t device_id,
                         input::DeviceDescriptor* descriptor,
                         OnEventCallback callback)
    : device_id_(device_id),
      descriptor_(descriptor),
      callback_(callback),
      keyboard_(this),
      mouse_(this),
      stylus_(this),
      touchscreen_(this),
      sensor_(this) {}

DeviceState::~DeviceState() {}

void DeviceState::OnRegistered() {
  if (descriptor_->keyboard) {
    keyboard_.OnRegistered();
  }
  if (descriptor_->mouse) {
    mouse_.OnRegistered();
  }
  if (descriptor_->stylus) {
    stylus_.OnRegistered();
  }
  if (descriptor_->touchscreen) {
    touchscreen_.OnRegistered();
  }
  if (descriptor_->sensor) {
    sensor_.OnRegistered();
  }
}

void DeviceState::OnUnregistered() {
  if (descriptor_->keyboard) {
    keyboard_.OnUnregistered();
  }
  if (descriptor_->mouse) {
    mouse_.OnUnregistered();
  }
  if (descriptor_->stylus) {
    stylus_.OnUnregistered();
  }
  if (descriptor_->touchscreen) {
    touchscreen_.OnUnregistered();
  }
  if (descriptor_->sensor) {
    sensor_.OnUnregistered();
  }
}

void DeviceState::Update(input::InputReport input_report,
                         geometry::Size display_size) {
  if (input_report.keyboard && descriptor_->keyboard) {
    keyboard_.Update(std::move(input_report));
  } else if (input_report.mouse && descriptor_->mouse) {
    mouse_.Update(std::move(input_report), display_size);
  } else if (input_report.stylus && descriptor_->stylus) {
    stylus_.Update(std::move(input_report), display_size);
  } else if (input_report.touchscreen && descriptor_->touchscreen) {
    touchscreen_.Update(std::move(input_report), display_size);
  } else if (input_report.sensor && descriptor_->sensor) {
    sensor_.Update(std::move(input_report));
  }
}

}  // namespace mozart
