// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/finish_thread_controller.h"

#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/client/until_thread_controller.h"
#include "garnet/bin/zxdb/common/err.h"
#include "lib/fxl/logging.h"

namespace zxdb {

FinishThreadController::FinishThreadController(FromFrame, const Frame* frame)
    : ThreadController(),
      frame_ip_(frame->GetAddress()),
      frame_bp_(frame->GetBasePointer()) {}

FinishThreadController::FinishThreadController(
    ToFrame, uint64_t to_address, const FrameFingerprint& to_frame_fingerprint)
    : to_address_(to_address),
      to_frame_fingerprint_(to_frame_fingerprint) {}

FinishThreadController::~FinishThreadController() = default;

FinishThreadController::StopOp FinishThreadController::OnThreadStop(
    debug_ipc::NotifyException::Type stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  return until_controller_->OnThreadStop(stop_type, hit_breakpoints);
}

void FinishThreadController::InitWithThread(
    Thread* thread, std::function<void(const Err&)> cb) {
  set_thread(thread);

  if (HaveAddressAndFingerprint()) {
    // The fingerprint was already computed in the constructor, can skip
    // directly to setting up the breakpoint.
    InitWithFingerprint(std::move(cb));
  } else {
    // Need to make sure the frames are available to find the fingerprint
    // (fingerprint computation requires both the destination frame and the
    // frame before the destination frame).
    auto frames = thread->GetFrames();
    if (thread->HasAllFrames()) {
      InitWithFrames(frames, std::move(cb));
    } else {
      // Need to asynchronously request the thread's frames. We can capture
      // |this| here since the thread owns this class.
      thread->SyncFrames([ this, cb = std::move(cb) ]() {
        InitWithFrames(this->thread()->GetFrames(), std::move(cb));
      });
    }
  }
}

ThreadController::ContinueOp FinishThreadController::GetContinueOp() {
  return until_controller_->GetContinueOp();
}

void FinishThreadController::InitWithFrames(
    const std::vector<Frame*>& frames, std::function<void(const Err&)> cb) {
  // Note if this was called asynchronously the thread could be resumed
  // and it could have no frames, or totally different ones.

  // Find the frame corresponding to the reqested one.
  constexpr size_t kNotFound = std::numeric_limits<size_t>::max();
  size_t requested_index = kNotFound;
  for (size_t i = 0; i < frames.size(); i++) {
    if (frames[i]->GetAddress() == frame_ip_ &&
        frames[i]->GetBasePointer() == frame_bp_) {
      requested_index = i;
      break;
    }
  }
  if (requested_index == kNotFound) {
    cb(Err("The stack changed before \"finish\" could start."));
    return;
  }

  if (requested_index == frames.size() - 1) {
    // "Finish" from the bottom-most stack frame just continues the
    // program to completion.
    cb(Err());
    return;
  }

  // The stack frame to exit to is just the next one up.
  size_t step_to_index = requested_index + 1;
  to_address_ = frames[step_to_index]->GetAddress();
  to_frame_fingerprint_ = thread()->GetFrameFingerprint(step_to_index);
  InitWithFingerprint(std::move(cb));
}

bool FinishThreadController::HaveAddressAndFingerprint() const {
  return to_address_ != 0 && to_frame_fingerprint_.is_valid();
}

void FinishThreadController::InitWithFingerprint(
    std::function<void(const Err&)> cb) {
  FXL_DCHECK(HaveAddressAndFingerprint());
  until_controller_ = std::make_unique<UntilThreadController>(
      InputLocation(to_address_), to_frame_fingerprint_);

  // Give the "until" controller a dummy callback and execute the callback
  // ASAP. The until controller executes the callback once it knows that the
  // breakpoint set has been complete (round-trip to the target system).
  //
  // Since we provide an address there's no weirdness with symbols and we don't
  // have to worry about matching 0 locations. If the breakpoint set fails, the
  // caller address is invalid and stepping is impossible so it doesn't matter.
  // We can run faster without waiting for the round-trip, and the IPC will
  // serialize so the breakpoint set happens before the thread resume.
  until_controller_->InitWithThread(thread(), [](const Err&) {});
  cb(Err());
}

}  // namespace zxdb
