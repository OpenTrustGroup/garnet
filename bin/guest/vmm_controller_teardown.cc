// SPDX-License-Identifier: BSD-3-Clause

#include "garnet/bin/guest/vmm_controller_teardown.h"

bool VmmTeardownLifecycle::RequestTeardown() {
  if (state_ != State::kIdle) {
    return false;
  }

  state_ = State::kScheduled;
  return true;
}

bool VmmTeardownLifecycle::BeginTeardown() {
  if (state_ != State::kScheduled) {
    return false;
  }

  state_ = State::kRunning;
  return true;
}

bool VmmTeardownLifecycle::FinishTeardown() {
  if (state_ != State::kScheduled && state_ != State::kRunning) {
    return false;
  }

  state_ = State::kFinished;
  return true;
}

bool VmmTeardownLifecycle::RecordLifecycleClose() {
  if (component_stop_requested_) {
    return false;
  }

  component_stop_requested_ = true;
  return true;
}

bool VmmTeardownLifecycle::CompleteComponentStop() {
  if (!component_stop_requested_ || component_stop_completed_) {
    return false;
  }
  if (teardown_active()) {
    return false;
  }

  component_stop_completed_ = true;
  return true;
}
