// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/until_thread_controller.h"

#include "garnet/bin/zxdb/client/breakpoint.h"
#include "garnet/bin/zxdb/client/breakpoint_settings.h"
#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/input_location.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/system.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "lib/fxl/logging.h"

namespace zxdb {

UntilThreadController::UntilThreadController(InputLocation location)
    : ThreadController(), location_(std::move(location)), weak_factory_(this) {}

UntilThreadController::UntilThreadController(InputLocation location,
                                             FrameFingerprint newest_frame)
    : ThreadController(),
      location_(std::move(location)),
      newest_threadhold_frame_(newest_frame),
      weak_factory_(this) {}

UntilThreadController::~UntilThreadController() {
  if (breakpoint_)
    GetSystem()->DeleteBreakpoint(breakpoint_.get());
}

void UntilThreadController::InitWithThread(Thread* thread,
                                           std::function<void(const Err&)> cb) {
  set_thread(thread);

  BreakpointSettings settings;
  settings.scope = BreakpointSettings::Scope::kThread;
  settings.scope_target = GetTarget();
  settings.scope_thread = thread;
  settings.location = std::move(location_);

  // Frame-tied triggers can't be one-shot because we need to check the stack
  // every time it triggers. In the non-frame case the one-shot breakpoint will
  // be slightly more efficient.
  settings.one_shot = !newest_threadhold_frame_.is_valid();

  breakpoint_ = GetSystem()->CreateNewInternalBreakpoint()->GetWeakPtr();
  // The breakpoint may post the callback asynchronously, so we can't be sure
  // this class is still alive when this callback is issued, even though we
  // destroy the breakpoint in the destructor.
  breakpoint_->SetSettings(
      settings, [ weak_this = weak_factory_.GetWeakPtr(), cb ](const Err& err) {
        if (weak_this)
          weak_this->OnBreakpointSet(err, std::move(cb));
      });
}

ThreadController::ContinueOp UntilThreadController::GetContinueOp() {
  // Stopping the thread is done via a breakpoint, so the thread can always be
  // resumed with no qualifications.
  return ContinueOp::Continue();
}

ThreadController::StopOp UntilThreadController::OnThreadStop(
    debug_ipc::NotifyException::Type stop_type,
    const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) {
  // Other controllers such as the StepOverRangeThreadController can use this
  // as a sub-controller. If the controllers don't care about breakpoint set
  // failures, they may start using the thread right away without waiting for
  // the callback in InitWithThread() to asynchronously complete (indicating
  // the breakpoint was set successfull).
  //
  // This is generally fine, we just need to be careful not to do anything in
  // OnBreakpointSet() that the code in this function depends on.
  if (!breakpoint_) {
    // Our internal breakpoint shouldn't be deleted out from under ourselves.
    FXL_NOTREACHED();
    return kContinue;
  }

  // Only care about stops if one of the breakpoints hit was ours.
  Breakpoint* our_breakpoint = breakpoint_.get();
  bool is_our_breakpoint = true;
  for (auto& hit : hit_breakpoints) {
    if (hit && hit.get() == our_breakpoint) {
      is_our_breakpoint = true;
      break;
    }
  }
  if (!is_our_breakpoint) {
    Log("Not our breakpoint.");
    return kContinue;
  }

  if (!newest_threadhold_frame_.is_valid()) {
    Log("No frame check required.");
    return kStop;
  }

  auto frames = thread()->GetFrames();
  if (frames.empty()) {
    FXL_NOTREACHED();  // Should always have a current frame on stop.
    return kStop;
  }

  FrameFingerprint current_frame = thread()->GetFrameFingerprint(0);
  if (FrameFingerprint::Newer(current_frame, newest_threadhold_frame_)) {
    Log("In newer frame, ignoring.");
    return kContinue;
  }
  Log("Found target frame (or older).");
  return kStop;
}

System* UntilThreadController::GetSystem() {
  return &thread()->session()->system();
}

Target* UntilThreadController::GetTarget() {
  return thread()->GetProcess()->GetTarget();
}

void UntilThreadController::OnBreakpointSet(
    const Err& err, std::function<void(const Err&)> cb) {
  // This may get called after the thread stop in some cases so don't do
  // anything important in this function. See OnThreadStop().
  if (err.has_error()) {
    // Breakpoint setting failed.
    cb(err);
  } else if (!breakpoint_ || breakpoint_->GetLocations().empty()) {
    // Setting the breakpoint may have resolved to no locations and the
    // breakpoint is now pending. For "until" this is not good because if the
    // user does "until SometyhingNonexistant" they would like to see the error
    // rather than have the thread transparently continue without stopping.
    cb(Err("Destination to run until matched no location."));
  } else {
    // Success, can continue the thread.
    cb(Err());
  }
}

}  // namespace zxdb
