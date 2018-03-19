// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_TESTS_MOCKS_H_
#define GARNET_LIB_UI_GFX_TESTS_MOCKS_H_

#include "garnet/lib/ui/gfx/displays/display_manager.h"
#include "garnet/lib/ui/gfx/engine/engine.h"
#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/engine/session_handler.h"
#include "lib/escher/flib/release_fence_signaller.h"

namespace scenic {
namespace gfx {
namespace test {

class SessionForTest : public Session {
 public:
  SessionForTest(SessionId id,
                 Engine* engine,
                 EventReporter* event_reporter,
                 ErrorReporter* error_reporter);

  virtual void TearDown() override;
};

class SessionHandlerForTest : public SessionHandler {
 public:
  SessionHandlerForTest(CommandDispatcherContext context,
                        Engine* engine,
                        SessionId session_id,
                        EventReporter* event_reporter,
                        ErrorReporter* error_reporter);

  // ui::gfx::Session interface methods.
  void Enqueue(::f1dl::VectorPtr<ui::CommandPtr> commands) override;
  void Present(uint64_t presentation_time,
               ::f1dl::VectorPtr<zx::event> acquire_fences,
               ::f1dl::VectorPtr<zx::event> release_fences,
               const ui::Session::PresentCallback& callback) override;

  // Return the number of Enqueue()/Present()/Connect() messages that have
  // been processed.
  uint32_t enqueue_count() const { return enqueue_count_; }
  uint32_t present_count() const { return present_count_; }

 private:
  std::atomic<uint32_t> enqueue_count_;
  std::atomic<uint32_t> present_count_;
};

class ReleaseFenceSignallerForTest : public escher::ReleaseFenceSignaller {
 public:
  ReleaseFenceSignallerForTest(
      escher::impl::CommandBufferSequencer* command_buffer_sequencer);

  void AddCPUReleaseFence(zx::event fence) override;

  uint32_t num_calls_to_add_cpu_release_fence() {
    return num_calls_to_add_cpu_release_fence_;
  }

 private:
  uint32_t num_calls_to_add_cpu_release_fence_ = 0;
};

class SessionManagerForTest : public SessionManager {
 public:
  SessionManagerForTest(UpdateScheduler* update_scheduler);

 private:
  std::unique_ptr<SessionHandler> CreateSessionHandler(
      CommandDispatcherContext context,
      Engine* engine,
      SessionId session_id,
      EventReporter* event_reporter,
      ErrorReporter* error_reporter) const override;
};

class EngineForTest : public Engine {
 public:
  EngineForTest(DisplayManager* display_manager,
                std::unique_ptr<escher::ReleaseFenceSignaller> r,
                escher::Escher* escher = nullptr);

 private:
  std::unique_ptr<SessionManager> InitializeSessionManager() override;
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_TESTS_MOCKS_H_
