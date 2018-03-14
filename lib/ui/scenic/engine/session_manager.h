// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_ENGINE_SESSION_MANAGER_H_
#define GARNET_LIB_UI_SCENIC_ENGINE_SESSION_MANAGER_H_

#include <set>
#include <unordered_map>

#include "garnet/lib/ui/mozart/command_dispatcher.h"

namespace mz {
class EventReporter;
class ErrorReporter;
}  // namespace mz

namespace scene_manager {

using SessionId = uint64_t;

class SessionHandler;
class Session;
class Engine;
class UpdateScheduler;

// Manages a collection of SessionHandlers.
// Tracks future updates requested by Sessions, and executes updates for a
// particular presentation time.
class SessionManager {
 public:
  // An UpdateScheduler is used by SessionManager to request a new frame, which
  // eventually results in a call to ApplyScheduledSessionUpdates(). Must not be
  // null.
  SessionManager(UpdateScheduler* update_scheduler);

  virtual ~SessionManager() = default;

  // Finds the session handler corresponding to the given id.
  SessionHandler* FindSession(SessionId id);

  size_t GetSessionCount() { return session_count_; }

  // Returns a SessionHandler, which is casted as a CommandDispatcher. Used by
  // ScenicSystem.
  std::unique_ptr<mz::CommandDispatcher> CreateCommandDispatcher(
      mz::CommandDispatcherContext context,
      Engine* engine);

  // Tell the FrameScheduler to schedule a frame, and remember the Session so
  // that we can tell it to apply updates when the FrameScheduler notifies us
  // via OnPrepareFrame().
  void ScheduleUpdateForSession(uint64_t presentation_time,
                                fxl::RefPtr<Session> session);

  // Executes updates that are schedule up to and including a given presentation
  // time. Returns true if rendering is needed.
  bool ApplyScheduledSessionUpdates(uint64_t presentation_time,
                                    uint64_t presentation_interval);

 private:
  friend class Session;
  friend class SessionHandler;

  // Destroys the session with the given id.
  void TearDownSession(SessionId id);

  virtual std::unique_ptr<SessionHandler> CreateSessionHandler(
      mz::CommandDispatcherContext context,
      Engine* engine,
      SessionId session_id,
      mz::EventReporter* event_reporter,
      mz::ErrorReporter* error_reporter) const;

  UpdateScheduler* const update_scheduler_;

  // Map of all the sessions.
  std::unordered_map<SessionId, SessionHandler*> session_manager_;
  size_t session_count_ = 0;
  SessionId next_session_id_ = 1;

  // Lists all Session that have updates to apply, sorted by the earliest
  // requested presentation time of each update.
  std::set<std::pair<uint64_t, fxl::RefPtr<Session>>> updatable_sessions_;
};

}  // namespace scene_manager

#endif  // GARNET_LIB_UI_SCENIC_ENGINE_SESSION_MANAGER_H_
