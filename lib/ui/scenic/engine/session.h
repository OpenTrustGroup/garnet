// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_ENGINE_SESSION_H_
#define GARNET_LIB_UI_SCENIC_ENGINE_SESSION_H_

#include <vector>

#include "garnet/lib/ui/mozart/util/error_reporter.h"
#include "garnet/lib/ui/scenic/engine/engine.h"
#include "garnet/lib/ui/scenic/engine/event_reporter.h"
#include "garnet/lib/ui/scenic/engine/resource_map.h"
#include "garnet/lib/ui/scenic/resources/memory.h"
#include "lib/escher/flib/fence_set_listener.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/tasks/task_runner.h"

namespace scene_manager {

using SessionId = uint64_t;

class Image;
using ImagePtr = ::fxl::RefPtr<Image>;

class ImageBase;
using ImageBasePtr = ::fxl::RefPtr<ImageBase>;

class ImagePipe;
using ImagePipePtr = ::fxl::RefPtr<ImagePipe>;

class Session;
using SessionPtr = ::fxl::RefPtr<Session>;

class Engine;
class SessionHandler;

// TODO: use unsafe ref-counting for better performance (our architecture
// guarantees that this is safe).
class Session : public fxl::RefCountedThreadSafe<Session> {
 public:
  Session(SessionId id,
          Engine* engine,
          EventReporter* event_reporter = nullptr,
          mz::ErrorReporter* error_reporter = mz::ErrorReporter::Default());
  virtual ~Session();

  // Apply the operation to the current session state.  Return true if
  // successful, and false if the op is somehow invalid.  In the latter case,
  // the Session is left unchanged.
  bool ApplyOp(const scenic::OpPtr& op);

  SessionId id() const { return id_; }
  Engine* engine() const { return engine_; }
  escher::Escher* escher() const { return engine_->escher(); }

  // Return the total number of existing resources associated with this Session.
  size_t GetTotalResourceCount() const { return resource_count_; }

  // Return the number of resources that a client can identify via a
  // scenic::ResourceId. This number is decremented when a ReleaseResourceOp is
  // applied.  However, the resource may continue to exist if it is referenced
  // by other resources.
  size_t GetMappedResourceCount() const { return resources_.size(); }

  // Session becomes invalid once TearDown is called.
  bool is_valid() const { return is_valid_; }

  mz::ErrorReporter* error_reporter() const;

  ResourceMap* resources() { return &resources_; }

  // Called by SessionHandler::Present().  Stashes the arguments without
  // applying them; they will later be applied by ApplyScheduledUpdates().
  bool ScheduleUpdate(uint64_t presentation_time,
                      ::f1dl::Array<scenic::OpPtr> ops,
                      ::f1dl::Array<zx::event> acquire_fences,
                      ::f1dl::Array<zx::event> release_fences,
                      const ui_mozart::Session::PresentCallback& callback);

  // Called by ImagePipe::PresentImage().  Stashes the arguments without
  // applying them; they will later be applied by ApplyScheduledUpdates().
  void ScheduleImagePipeUpdate(uint64_t presentation_time,
                               ImagePipePtr image_pipe);

  // Called by Engine() when it is notified by the FrameScheduler that
  // a frame should be rendered for the specified |presentation_time|.  Return
  // true if any updates were applied, and false otherwise.
  bool ApplyScheduledUpdates(uint64_t presentation_time,
                             uint64_t presentation_interval);

  // Add an event to our queue, which will be scheduled to be flushed and sent
  // to the event reporter later.
  void EnqueueEvent(scenic::EventPtr event);

  // Called by SessionHandler::HitTest().
  void HitTest(uint32_t node_id,
               scenic::vec3Ptr ray_origin,
               scenic::vec3Ptr ray_direction,
               const ui_mozart::Session::HitTestCallback& callback);

  // Called by SessionHandler::HitTestDeviceRay().
  void HitTestDeviceRay(scenic::vec3Ptr ray_origin,
                        scenic::vec3Ptr ray_direction,
                        const ui_mozart::Session::HitTestCallback& callback);

 protected:
  friend class SessionHandler;
  // Called only by SessionHandler. Use BeginTearDown() instead when you need to
  // teardown from within Session. Virtual to allow test subclasses to override.
  //
  // The chain of events is:
  // Session::BeginTearDown or SessionHandler::BeginTearDown
  // => Engine::TearDownSession
  // => SessionHandler::TearDown
  // => Session::TearDown
  //
  // We are guaranteed that by the time TearDown() is closed, SessionHandler
  // has destroyed the channel to this session.
  virtual void TearDown();

 private:
  // Called internally to initiate teardown.
  void BeginTearDown();

  // Operation application functions, called by ApplyOp().
  bool ApplyCreateResourceOp(const scenic::CreateResourceOpPtr& op);
  bool ApplyReleaseResourceOp(const scenic::ReleaseResourceOpPtr& op);
  bool ApplyExportResourceOp(const scenic::ExportResourceOpPtr& op);
  bool ApplyImportResourceOp(const scenic::ImportResourceOpPtr& op);
  bool ApplyAddChildOp(const scenic::AddChildOpPtr& op);
  bool ApplyAddPartOp(const scenic::AddPartOpPtr& op);
  bool ApplyDetachOp(const scenic::DetachOpPtr& op);
  bool ApplyDetachChildrenOp(const scenic::DetachChildrenOpPtr& op);
  bool ApplySetTagOp(const scenic::SetTagOpPtr& op);
  bool ApplySetTranslationOp(const scenic::SetTranslationOpPtr& op);
  bool ApplySetScaleOp(const scenic::SetScaleOpPtr& op);
  bool ApplySetRotationOp(const scenic::SetRotationOpPtr& op);
  bool ApplySetAnchorOp(const scenic::SetAnchorOpPtr& op);
  bool ApplySetSizeOp(const scenic::SetSizeOpPtr& op);
  bool ApplySetShapeOp(const scenic::SetShapeOpPtr& op);
  bool ApplySetMaterialOp(const scenic::SetMaterialOpPtr& op);
  bool ApplySetClipOp(const scenic::SetClipOpPtr& op);
  bool ApplySetHitTestBehaviorOp(const scenic::SetHitTestBehaviorOpPtr& op);
  bool ApplySetCameraOp(const scenic::SetCameraOpPtr& op);
  bool ApplySetCameraProjectionOp(const scenic::SetCameraProjectionOpPtr& op);
  bool ApplySetCameraPoseBufferOp(const scenic::SetCameraPoseBufferOpPtr& op);
  bool ApplySetLightColorOp(const scenic::SetLightColorOpPtr& op);
  bool ApplySetLightDirectionOp(const scenic::SetLightDirectionOpPtr& op);
  bool ApplyAddLightOp(const scenic::AddLightOpPtr& op);
  bool ApplyDetachLightOp(const scenic::DetachLightOpPtr& op);
  bool ApplyDetachLightsOp(const scenic::DetachLightsOpPtr& op);
  bool ApplySetTextureOp(const scenic::SetTextureOpPtr& op);
  bool ApplySetColorOp(const scenic::SetColorOpPtr& op);
  bool ApplyBindMeshBuffersOp(const scenic::BindMeshBuffersOpPtr& op);
  bool ApplyAddLayerOp(const scenic::AddLayerOpPtr& op);
  bool ApplySetLayerStackOp(const scenic::SetLayerStackOpPtr& op);
  bool ApplySetRendererOp(const scenic::SetRendererOpPtr& op);
  bool ApplySetRendererParamOp(const scenic::SetRendererParamOpPtr& op);
  bool ApplySetEventMaskOp(const scenic::SetEventMaskOpPtr& op);
  bool ApplySetLabelOp(const scenic::SetLabelOpPtr& op);
  bool ApplySetDisableClippingOp(const scenic::SetDisableClippingOpPtr& op);

  // Resource creation functions, called by ApplyCreateResourceOp().
  bool ApplyCreateMemory(scenic::ResourceId id, const scenic::MemoryPtr& args);
  bool ApplyCreateImage(scenic::ResourceId id, const scenic::ImagePtr& args);
  bool ApplyCreateImagePipe(scenic::ResourceId id,
                            const scenic::ImagePipeArgsPtr& args);
  bool ApplyCreateBuffer(scenic::ResourceId id, const scenic::BufferPtr& args);
  bool ApplyCreateScene(scenic::ResourceId id, const scenic::ScenePtr& args);
  bool ApplyCreateCamera(scenic::ResourceId id, const scenic::CameraPtr& args);
  bool ApplyCreateRenderer(scenic::ResourceId id,
                           const scenic::RendererPtr& args);
  bool ApplyCreateAmbientLight(scenic::ResourceId id,
                               const scenic::AmbientLightPtr& args);
  bool ApplyCreateDirectionalLight(scenic::ResourceId id,
                                   const scenic::DirectionalLightPtr& args);
  bool ApplyCreateRectangle(scenic::ResourceId id,
                            const scenic::RectanglePtr& args);
  bool ApplyCreateRoundedRectangle(scenic::ResourceId id,
                                   const scenic::RoundedRectanglePtr& args);
  bool ApplyCreateCircle(scenic::ResourceId id, const scenic::CirclePtr& args);
  bool ApplyCreateMesh(scenic::ResourceId id, const scenic::MeshPtr& args);
  bool ApplyCreateMaterial(scenic::ResourceId id,
                           const scenic::MaterialPtr& args);
  bool ApplyCreateClipNode(scenic::ResourceId id,
                           const scenic::ClipNodePtr& args);
  bool ApplyCreateEntityNode(scenic::ResourceId id,
                             const scenic::EntityNodePtr& args);
  bool ApplyCreateShapeNode(scenic::ResourceId id,
                            const scenic::ShapeNodePtr& args);
  bool ApplyCreateDisplayCompositor(scenic::ResourceId id,
                                    const scenic::DisplayCompositorPtr& args);
  bool ApplyCreateImagePipeCompositor(
      scenic::ResourceId id,
      const scenic::ImagePipeCompositorPtr& args);
  bool ApplyCreateLayerStack(scenic::ResourceId id,
                             const scenic::LayerStackPtr& args);
  bool ApplyCreateLayer(scenic::ResourceId id, const scenic::LayerPtr& args);
  bool ApplyCreateVariable(scenic::ResourceId id,
                           const scenic::VariablePtr& args);

  // Actually create resources.
  ResourcePtr CreateMemory(scenic::ResourceId id,
                           const scenic::MemoryPtr& args);
  ResourcePtr CreateImage(scenic::ResourceId id,
                          MemoryPtr memory,
                          const scenic::ImagePtr& args);
  ResourcePtr CreateBuffer(scenic::ResourceId id,
                           MemoryPtr memory,
                           uint32_t memory_offset,
                           uint32_t num_bytes);
  ResourcePtr CreateScene(scenic::ResourceId id, const scenic::ScenePtr& args);
  ResourcePtr CreateCamera(scenic::ResourceId id,
                           const scenic::CameraPtr& args);
  ResourcePtr CreateRenderer(scenic::ResourceId id,
                             const scenic::RendererPtr& args);
  ResourcePtr CreateAmbientLight(scenic::ResourceId id);
  ResourcePtr CreateDirectionalLight(scenic::ResourceId id);
  ResourcePtr CreateClipNode(scenic::ResourceId id,
                             const scenic::ClipNodePtr& args);
  ResourcePtr CreateEntityNode(scenic::ResourceId id,
                               const scenic::EntityNodePtr& args);
  ResourcePtr CreateShapeNode(scenic::ResourceId id,
                              const scenic::ShapeNodePtr& args);
  ResourcePtr CreateDisplayCompositor(scenic::ResourceId id,
                                      const scenic::DisplayCompositorPtr& args);
  ResourcePtr CreateImagePipeCompositor(
      scenic::ResourceId id,
      const scenic::ImagePipeCompositorPtr& args);
  ResourcePtr CreateLayerStack(scenic::ResourceId id,
                               const scenic::LayerStackPtr& args);
  ResourcePtr CreateLayer(scenic::ResourceId id, const scenic::LayerPtr& args);
  ResourcePtr CreateCircle(scenic::ResourceId id, float initial_radius);
  ResourcePtr CreateRectangle(scenic::ResourceId id, float width, float height);
  ResourcePtr CreateRoundedRectangle(scenic::ResourceId id,
                                     float width,
                                     float height,
                                     float top_left_radius,
                                     float top_right_radius,
                                     float bottom_right_radius,
                                     float bottom_left_radius);
  ResourcePtr CreateMesh(scenic::ResourceId id);
  ResourcePtr CreateMaterial(scenic::ResourceId id);
  ResourcePtr CreateVariable(scenic::ResourceId id,
                             const scenic::VariablePtr& args);

  // Return false and log an error if the value is not of the expected type.
  // NOTE: although failure does not halt execution of the program, it does
  // indicate client error, and will be used by the caller to tear down the
  // Session.
  bool AssertValueIsOfType(const scenic::ValuePtr& value,
                           const scenic::Value::Tag* tags,
                           size_t tag_count);
  template <size_t N>
  bool AssertValueIsOfType(const scenic::ValuePtr& value,
                           const std::array<scenic::Value::Tag, N>& tags) {
    return AssertValueIsOfType(value, tags.data(), N);
  }

  void FlushEvents();

  friend class Resource;
  void IncrementResourceCount() { ++resource_count_; }
  void DecrementResourceCount() { --resource_count_; }

  struct Update {
    uint64_t presentation_time;

    ::f1dl::Array<scenic::OpPtr> ops;
    std::unique_ptr<escher::FenceSetListener> acquire_fences;
    ::f1dl::Array<zx::event> release_fences;

    // Callback to report when the update has been applied in response to
    // an invocation of |Session.Present()|.
    ui_mozart::Session::PresentCallback present_callback;
  };
  bool ApplyUpdate(Update* update);
  std::queue<Update> scheduled_updates_;
  ::f1dl::Array<zx::event> fences_to_release_on_next_update_;

  uint64_t last_applied_update_presentation_time_ = 0;
  uint64_t last_presentation_time_ = 0;

  struct ImagePipeUpdate {
    uint64_t presentation_time;
    ImagePipePtr image_pipe;

    bool operator<(const ImagePipeUpdate& rhs) const {
      return presentation_time < rhs.presentation_time;
    }
  };
  std::priority_queue<ImagePipeUpdate> scheduled_image_pipe_updates_;
  ::f1dl::Array<scenic::EventPtr> buffered_events_;

  const SessionId id_;
  Engine* const engine_;
  mz::ErrorReporter* error_reporter_ = nullptr;
  EventReporter* event_reporter_ = nullptr;

  ResourceMap resources_;

  size_t resource_count_ = 0;
  bool is_valid_ = true;

  fxl::WeakPtrFactory<Session> weak_factory_;  // must be last
};

}  // namespace scene_manager

#endif  // GARNET_LIB_UI_SCENIC_ENGINE_SESSION_H_
