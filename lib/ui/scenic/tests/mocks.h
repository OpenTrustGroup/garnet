// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_TESTS_MOCKS_H_
#define GARNET_LIB_UI_SCENIC_TESTS_MOCKS_H_

#include "garnet/bin/ui/scene_manager/scene_manager_impl.h"
#include "garnet/lib/ui/scenic/displays/display_manager.h"
#include "garnet/lib/ui/scenic/engine/engine.h"
#include "garnet/lib/ui/scenic/engine/session.h"
#include "garnet/lib/ui/scenic/engine/session_handler.h"
#include "lib/escher/flib/release_fence_signaller.h"

namespace scene_manager {
namespace test {

class SessionForTest : public Session {
 public:
  SessionForTest(SessionId id,
                 Engine* engine,
                 scene_manager::EventReporter* event_reporter,
                 scene_manager::ErrorReporter* error_reporter);

  virtual void TearDown() override;
};

class SessionHandlerForTest : public SessionHandler {
 public:
  SessionHandlerForTest(
      Engine* engine,
      SessionId session_id,
      ::fidl::InterfaceRequest<scenic::Session> request,
      ::fidl::InterfaceHandle<scenic::SessionListener> listener);

  // scenic::Session interface methods.
  void Enqueue(::fidl::Array<scenic::OpPtr> ops) override;
  void Present(uint64_t presentation_time,
               ::fidl::Array<zx::event> acquire_fences,
               ::fidl::Array<zx::event> release_fences,
               const PresentCallback& callback) override;

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

class EngineForTest : public Engine {
 public:
  EngineForTest(DisplayManager* display_manager,
                std::unique_ptr<escher::ReleaseFenceSignaller> r,
                escher::Escher* escher = nullptr);
  using Engine::FindSession;

 private:
  std::unique_ptr<SessionHandler> CreateSessionHandler(
      SessionId id,
      ::fidl::InterfaceRequest<scenic::Session> request,
      ::fidl::InterfaceHandle<scenic::SessionListener> listener) override;
};

}  // namespace test
}  // namespace scene_manager

#endif  // GARNET_LIB_UI_SCENIC_TESTS_MOCKS_H_
