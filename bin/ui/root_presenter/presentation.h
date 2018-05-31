// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ROOT_PRESENTER_PRESENTATION_H_
#define GARNET_BIN_UI_ROOT_PRESENTER_PRESENTATION_H_

#include <map>
#include <memory>

#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <presentation/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>

#include "garnet/bin/ui/presentation_mode/detector.h"
#include "garnet/bin/ui/root_presenter/display_rotater.h"
#include "garnet/bin/ui/root_presenter/display_size_switcher.h"
#include "garnet/bin/ui/root_presenter/display_usage_switcher.h"
#include "garnet/bin/ui/root_presenter/displays/display_metrics.h"
#include "garnet/bin/ui/root_presenter/displays/display_model.h"
#include "garnet/bin/ui/root_presenter/perspective_demo_mode.h"
#include "garnet/bin/ui/root_presenter/presentation_switcher.h"
#include "garnet/bin/ui/root_presenter/renderer_params.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/ui/input/device_state.h"
#include "lib/ui/input/input_device_impl.h"
#include "lib/ui/scenic/client/resources.h"
#if defined(countof)
// Workaround for compiler error due to Zircon defining countof() as a macro.
// Redefines countof() using GLM_COUNTOF(), which currently provides a more
// sophisticated implementation anyway.
#undef countof
#include <glm/glm.hpp>
#define countof(X) GLM_COUNTOF(X)
#else
// No workaround required.
#include <glm/glm.hpp>
#endif

namespace root_presenter {

// This class creates a view tree and sets up rendering of a new scene to
// display the graphical content of the view passed to |PresentScene()|.  It
// also wires up input dispatch and manages the mouse cursor.
//
// The view tree consists of a root view which is implemented by this class
// and which has the presented (content) view as its child.
//
// The scene's node tree has the following structure:
// + Scene
//   + RootViewHost
//     + link: root_view_host_import_token
//       + RootView's view manager stub
//         + link: root_view_parent_export_token
//           + RootView
//             + link: content_view_host_import_token
//               + child: ContentViewHost
//           + link: Content view's actual content
//   + child: cursor 1
//   + child: cursor N
class Presentation : private ::fuchsia::ui::views_v1::ViewTreeListener,
                     private ::fuchsia::ui::views_v1::ViewListener,
                     private ::fuchsia::ui::views_v1::ViewContainerListener,
                     private presentation::Presentation {
 public:
  // Callback when the presentation yields to the next/previous one.
  using YieldCallback = std::function<void(bool yield_to_next)>;
  // Callback when the presentation is shut down.
  using ShutdownCallback = std::function<void()>;

  Presentation(::fuchsia::ui::views_v1::ViewManager* view_manager,
               fuchsia::ui::scenic::Scenic* scenic,
               scenic_lib::Session* session, RendererParams renderer_params);

  ~Presentation() override;

  // Present the specified view.
  // Invokes the callback if an error occurs.
  // This method must be called at most once for the lifetime of the
  // presentation.
  void Present(
      ::fuchsia::ui::views_v1_token::ViewOwnerPtr view_owner,
      fidl::InterfaceRequest<presentation::Presentation> presentation_request,
      YieldCallback yield_callback, ShutdownCallback shutdown_callback);

  void OnReport(uint32_t device_id, fuchsia::ui::input::InputReport report);
  void OnDeviceAdded(mozart::InputDeviceImpl* input_device);
  void OnDeviceRemoved(uint32_t device_id);

  const scenic_lib::Layer& layer() const { return layer_; }

 private:
  enum SessionPresentState {
    kNoPresentPending,
    kPresentPending,
    kPresentPendingAndSceneDirty
  };
  friend class DisplayRotater;
  friend class DisplayUsageSwitcher;
  friend class PerspectiveDemoMode;
  friend class DisplaySizeSwitcher;
  friend class PresentationSwitcher;

  // Sets |display_metrics_| and updates view_manager and Scenic.
  // Returns false if the updates were skipped (if display initialization hasn't
  // happened yet).
  bool ApplyDisplayModelChanges(bool print_log);

  // |ViewContainerListener|:
  void OnChildAttached(uint32_t child_key, ::fuchsia::ui::views_v1::ViewInfo child_view_info,
                       OnChildAttachedCallback callback) override;
  void OnChildUnavailable(uint32_t child_key,
                          OnChildUnavailableCallback callback) override;

  // |ViewListener|:
  void OnPropertiesChanged(::fuchsia::ui::views_v1::ViewProperties properties,
                           OnPropertiesChangedCallback callback) override;

  // |Presentation|
  void EnableClipping(bool enabled) override;

  // |Presentation|
  void UseOrthographicView() override;

  // |Presentation|
  void UsePerspectiveView() override;

  // |Presentation|
  void SetRendererParams(
      ::fidl::VectorPtr<fuchsia::ui::gfx::RendererParam> params) override;

  void InitializeDisplayModel(fuchsia::ui::gfx::DisplayInfo display_info);

  // |Presentation|
  void SetDisplayUsage(presentation::DisplayUsage usage) override;

  void SetDisplayUsageWithoutApplyingChanges(presentation::DisplayUsage usage_);

  // |Presentation|
  void SetDisplaySizeInMm(float width_in_mm, float height_in_mm) override;

  // |Presentation|
  void SetDisplayRotation(float display_rotation_degrees,
                          bool animate) override;

  // Returns false if the operation failed (e.g. the requested size is bigger
  // than the actual display size).
  bool SetDisplaySizeInMmWithoutApplyingChanges(float width_in_mm,
                                                float height_in_mm,
                                                bool print_errors);

  // |Presentation|
  void CaptureKeyboardEventHACK(
      fuchsia::ui::input::KeyboardEvent event_to_capture,
      fidl::InterfaceHandle<presentation::KeyboardCaptureListenerHACK> listener)
      override;

  // |Presentation|
  void CapturePointerEventsHACK(
      fidl::InterfaceHandle<presentation::PointerCaptureListenerHACK> listener)
      override;

  // |Presentation|
  void GetPresentationMode(GetPresentationModeCallback callback) override;

  // |Presentation|
  void SetPresentationModeListener(
      fidl::InterfaceHandle<presentation::PresentationModeListener> listener)
      override;

  void CreateViewTree(
      ::fuchsia::ui::views_v1_token::ViewOwnerPtr view_owner,
      fidl::InterfaceRequest<presentation::Presentation> presentation_request,
      fuchsia::ui::gfx::DisplayInfo display_info);

  // Returns true if the event was consumed and the scene is to be invalidated.
  bool GlobalHooksHandleEvent(const fuchsia::ui::input::InputEvent& event);

  void OnEvent(fuchsia::ui::input::InputEvent event);
  void OnSensorEvent(uint32_t device_id, fuchsia::ui::input::InputReport event);

  void PresentScene();
  void Shutdown();

  ::fuchsia::ui::views_v1::ViewManager* const view_manager_;
  fuchsia::ui::scenic::Scenic* const scenic_;
  scenic_lib::Session* const session_;

  scenic_lib::Layer layer_;
  scenic_lib::Renderer renderer_;
  // TODO(MZ-254): put camera before scene.
  scenic_lib::Scene scene_;
  scenic_lib::Camera camera_;
  scenic_lib::AmbientLight ambient_light_;
  glm::vec3 light_direction_;
  scenic_lib::DirectionalLight directional_light_;
  scenic_lib::EntityNode root_view_host_node_;
  zx::eventpair root_view_host_import_token_;
  scenic_lib::ImportNode root_view_parent_node_;
  zx::eventpair root_view_parent_export_token_;
  scenic_lib::EntityNode content_view_host_node_;
  zx::eventpair content_view_host_import_token_;
  scenic_lib::RoundedRectangle cursor_shape_;
  scenic_lib::Material cursor_material_;

  SessionPresentState session_present_state_ = kNoPresentPending;

  bool presentation_clipping_enabled_ = true;

  bool display_model_initialized_ = false;

  DisplayModel display_model_actual_;
  DisplayModel display_model_simulated_;

  // When |display_model_simulated_| or |display_rotation_desired_| changes:
  //   * |display_metrics_| must be recalculated.
  //   * |display_rotation_current_| must be updated.
  //   * Transforms on the scene must be updated.
  // This is done by calling ApplyDisplayModelChanges().
  DisplayMetrics display_metrics_;

  // Expressed in degrees.
  float display_rotation_desired_ = 0.f;
  float display_rotation_current_ = 0.f;

  ::fuchsia::ui::views_v1::ViewPtr root_view_;

  YieldCallback yield_callback_;
  ShutdownCallback shutdown_callback_;

  fuchsia::math::PointF mouse_coordinates_;

  fidl::Binding<presentation::Presentation> presentation_binding_;
  fidl::Binding<::fuchsia::ui::views_v1::ViewTreeListener> tree_listener_binding_;
  fidl::Binding<::fuchsia::ui::views_v1::ViewContainerListener>
      tree_container_listener_binding_;
  fidl::Binding<::fuchsia::ui::views_v1::ViewContainerListener>
      view_container_listener_binding_;
  fidl::Binding<::fuchsia::ui::views_v1::ViewListener> view_listener_binding_;

  ::fuchsia::ui::views_v1::ViewTreePtr tree_;
  ::fuchsia::ui::views_v1::ViewContainerPtr tree_container_;
  ::fuchsia::ui::views_v1::ViewContainerPtr root_container_;
  fuchsia::ui::input::InputDispatcherPtr input_dispatcher_;

  // Rotates the display 180 degrees in response to events.
  DisplayRotater display_rotater_;

  // Toggles through different display usage values.
  DisplayUsageSwitcher display_usage_switcher_;

  PerspectiveDemoMode perspective_demo_mode_;

  // Toggles through different display sizes.
  DisplaySizeSwitcher display_size_switcher_;

  // Toggles through different presentations.
  PresentationSwitcher presentation_switcher_;

  // Stores values that, if set, override any renderer params.
  RendererParams renderer_params_override_;

  struct CursorState {
    bool created;
    bool visible;
    fuchsia::math::PointF position;
    std::unique_ptr<scenic_lib::ShapeNode> node;
  };

  std::map<uint32_t, CursorState> cursors_;
  std::map<uint32_t, std::pair<mozart::InputDeviceImpl*,
                               std::unique_ptr<mozart::DeviceState>>>
      device_states_by_id_;

  // A registry of listeners who want to be notified when their keyboard
  // event happens.
  struct KeyboardCaptureItem {
    fuchsia::ui::input::KeyboardEvent event;
    presentation::KeyboardCaptureListenerHACKPtr listener;
  };
  std::vector<KeyboardCaptureItem> captured_keybindings_;

  // A registry of listeners who want to be notified when pointer event happens.
  struct PointerCaptureItem {
    presentation::PointerCaptureListenerHACKPtr listener;
  };
  std::vector<PointerCaptureItem> captured_pointerbindings_;

  // Listener for changes in presentation mode.
  presentation::PresentationModeListenerPtr presentation_mode_listener_;
  // Presentation mode, based on last N measurements
  presentation::PresentationMode presentation_mode_;
  std::unique_ptr<presentation_mode::Detector> presentation_mode_detector_;

  fxl::WeakPtrFactory<Presentation> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Presentation);
};

}  // namespace root_presenter

#endif  // GARNET_BIN_UI_ROOT_PRESENTER_PRESENTATION_H_
