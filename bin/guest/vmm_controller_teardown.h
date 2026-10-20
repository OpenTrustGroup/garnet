// SPDX-License-Identifier: BSD-3-Clause

#pragma once

class VmmTeardownLifecycle {
 public:
  enum class State {
    kIdle,
    kScheduled,
    kRunning,
    kFinished,
  };

  bool RequestTeardown();
  bool BeginTeardown();
  bool FinishTeardown();

  bool RecordLifecycleClose();
  bool CompleteComponentStop();

  bool teardown_started() const { return state_ != State::kIdle; }
  bool teardown_active() const {
    return state_ == State::kScheduled || state_ == State::kRunning;
  }
  bool create_blocked() const { return teardown_started(); }
  bool component_stop_requested() const { return component_stop_requested_; }
  bool component_stop_completed() const { return component_stop_completed_; }

  State state() const { return state_; }

 private:
  State state_ = State::kIdle;
  bool component_stop_requested_ = false;
  bool component_stop_completed_ = false;
};
