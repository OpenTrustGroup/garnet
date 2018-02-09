// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_ENGINE_ENGINE_H_
#define GARNET_LIB_UI_SCENIC_ENGINE_ENGINE_H_

#include <set>
#include <vector>

#include "lib/escher/escher.h"
#include "lib/escher/flib/release_fence_signaller.h"
#include "lib/escher/impl/gpu_uploader.h"
#include "lib/escher/resources/resource_recycler.h"
#include "lib/escher/shape/rounded_rect_factory.h"
#include "lib/escher/vk/simple_image_factory.h"

#include "garnet/lib/ui/scenic/displays/display_manager.h"
#include "garnet/lib/ui/scenic/engine/frame_scheduler.h"
#include "garnet/lib/ui/scenic/engine/resource_linker.h"
#include "garnet/lib/ui/scenic/resources/import.h"
#include "garnet/lib/ui/scenic/resources/nodes/scene.h"
#include "garnet/lib/ui/scenic/util/event_timestamper.h"

namespace scene_manager {

using SessionId = uint64_t;

class Compositor;
class Session;
class SessionHandler;
class Swapchain;

// Owns a group of sessions which can share resources with one another
// using the same resource linker and which coexist within the same timing
// domain using the same frame scheduler.  It is not possible for sessions
// which belong to different engines to communicate with one another.
class Engine : private FrameSchedulerDelegate {
 public:
  Engine(DisplayManager* display_manager, escher::Escher* escher);

  ~Engine() override;

  DisplayManager* display_manager() const { return display_manager_; }
  escher::Escher* escher() const { return escher_; }

  vk::Device vk_device() {
    return escher_ ? escher_->vulkan_context().device : vk::Device();
  }

  escher::ResourceRecycler* escher_resource_recycler() {
    return escher_ ? escher_->resource_recycler() : nullptr;
  }

  escher::ImageFactory* escher_image_factory() { return image_factory_.get(); }

  escher::impl::GpuUploader* escher_gpu_uploader() {
    return escher_ ? escher_->gpu_uploader() : nullptr;
  }

  escher::RoundedRectFactory* escher_rounded_rect_factory() {
    return rounded_rect_factory_.get();
  }

  escher::ReleaseFenceSignaller* release_fence_signaller() {
    return release_fence_signaller_.get();
  }

  ResourceLinker* resource_linker() { return &resource_linker_; }

  EventTimestamper* event_timestamper() { return &event_timestamper_; }

  // Tell the FrameScheduler to schedule a frame, and remember the Session so
  // that we can tell it to apply updates when the FrameScheduler notifies us
  // via OnPrepareFrame().
  void ScheduleSessionUpdate(uint64_t presentation_time,
                             fxl::RefPtr<Session> session);

  // Tell the FrameScheduler to schedule a frame. This is used for updates
  // triggered by something other than a Session update i.e. an ImagePipe with
  // a new Image to present.
  void ScheduleUpdate(uint64_t presentation_time);

  void CreateSession(::fidl::InterfaceRequest<scenic::Session> request,
                     ::fidl::InterfaceHandle<scenic::SessionListener> listener);

  // Create a swapchain for the specified display.  The display must not already
  // be claimed by another swapchain.
  std::unique_ptr<Swapchain> CreateDisplaySwapchain(Display* display);

  // Finds the session handler corresponding to the given id.
  SessionHandler* FindSession(SessionId id);

  size_t GetSessionCount() { return session_count_; }

  // Returns the first compositor in the current compositors, or nullptr if no
  // compositor exists.
  Compositor* GetFirstCompositor() const;

  // Dumps the contents of all scene graphs.
  std::string DumpScenes() const;

 protected:
  // Only used by subclasses used in testing.
  Engine(DisplayManager* display_manager,
         std::unique_ptr<escher::ReleaseFenceSignaller> release_fence_signaller,
         escher::Escher* escher);

 private:
  friend class Compositor;
  friend class SessionHandler;
  friend class Session;

  // Compositors register/unregister themselves upon creation/destruction.
  void AddCompositor(Compositor* compositor);
  void RemoveCompositor(Compositor* compositor);

  // Allow overriding to support tests.
  virtual std::unique_ptr<SessionHandler> CreateSessionHandler(
      SessionId id,
      ::fidl::InterfaceRequest<scenic::Session> request,
      ::fidl::InterfaceHandle<scenic::SessionListener> listener);

  // Destroys the session with the given id.
  void TearDownSession(SessionId id);

  // |FrameSchedulerDelegate|:
  bool RenderFrame(const FrameTimingsPtr& frame,
                   uint64_t presentation_time,
                   uint64_t presentation_interval) override;

  // Returns true if rendering is needed.
  bool ApplyScheduledSessionUpdates(uint64_t presentation_time,
                                    uint64_t presentation_interval);

  void InitializeFrameScheduler();

  // Update and deliver metrics for all nodes which subscribe to metrics events.
  void UpdateAndDeliverMetrics(uint64_t presentation_time);

  // Update reported metrics for nodes which subscribe to metrics events.
  // If anything changed, append the node to |updated_nodes|.
  void UpdateMetrics(Node* node,
                     const scenic::Metrics& parent_metrics,
                     std::vector<Node*>* updated_nodes);

  // Invoke Escher::Cleanup().  If more work remains afterward, post a delayed
  // task to try again; this is typically because cleanup couldn't finish due to
  // unfinished GPU work.
  void CleanupEscher();

  DisplayManager* const display_manager_;
  escher::Escher* const escher_;
  escher::PaperRendererPtr paper_renderer_;
  escher::ShadowMapRendererPtr shadow_renderer_;

  ResourceLinker resource_linker_;
  EventTimestamper event_timestamper_;
  std::unique_ptr<escher::SimpleImageFactory> image_factory_;
  std::unique_ptr<escher::RoundedRectFactory> rounded_rect_factory_;
  std::unique_ptr<escher::ReleaseFenceSignaller> release_fence_signaller_;
  std::unique_ptr<FrameScheduler> frame_scheduler_;
  std::set<Compositor*> compositors_;

  // Map of all the sessions.
  std::unordered_map<SessionId, std::unique_ptr<SessionHandler>> sessions_;
  std::atomic<size_t> session_count_;
  SessionId next_session_id_ = 1;

  bool escher_cleanup_scheduled_ = false;

  // Lists all Session that have updates to apply, sorted by the earliest
  // requested presentation time of each update.
  std::set<std::pair<uint64_t, fxl::RefPtr<Session>>> updatable_sessions_;

  fxl::WeakPtrFactory<Engine> weak_factory_;  // must be last

  FXL_DISALLOW_COPY_AND_ASSIGN(Engine);
};

}  // namespace scene_manager

#endif  // GARNET_LIB_UI_SCENIC_ENGINE_ENGINE_H_
