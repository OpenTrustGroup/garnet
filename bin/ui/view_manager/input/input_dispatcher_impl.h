// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_INPUT_INPUT_DISPATCHER_IMPL_H_
#define GARNET_BIN_UI_VIEW_MANAGER_INPUT_INPUT_DISPATCHER_IMPL_H_

#include <queue>
#include <set>
#include <utility>

#include <fuchsia/cpp/geometry.h>
#include <fuchsia/cpp/input.h>
#include <fuchsia/cpp/views_v1.h>
#include "garnet/bin/ui/view_manager/internal/view_inspector.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace view_manager {

class ViewInspector;
class InputOwner;

// InputDispatcher implementation.
// Binds incoming requests to the relevant view token.
class InputDispatcherImpl : public input::InputDispatcher {
 public:
  InputDispatcherImpl(ViewInspector* inspector,
                      InputOwner* owner,
                      views_v1::ViewTreeToken view_tree_token,
                      fidl::InterfaceRequest<input::InputDispatcher> request);
  ~InputDispatcherImpl() override;

  views_v1::ViewTreeToken view_tree_token() const { return view_tree_token_; }

  // |input::InputDispatcher|
  void DispatchEvent(input::InputEvent event) override;

 private:
  void ProcessNextEvent();
  // Used for located events (touch, stylus)
  void DeliverEvent(input::InputEvent event);
  void DeliverEvent(uint64_t event_path_propagation_id,
                    size_t index,
                    input::InputEvent event);
  // Used for key events (keyboard)
  // |propagation_index| is the current index in the |focus_chain|
  void DeliverKeyEvent(std::unique_ptr<FocusChain> focus_chain,
                       uint64_t propagation_index,
                       input::InputEvent event);
  // Used to post as task and schedule the next call to |DispatchEvent|
  void PopAndScheduleNextEvent();

  void OnFocusResult(std::unique_ptr<FocusChain> focus_chain);
  void OnHitTestResult(const geometry::PointF& point,
                       std::vector<ViewHit> view_hits);

  ViewInspector* const inspector_;
  InputOwner* const owner_;
  views_v1::ViewTreeToken view_tree_token_;

  // TODO(jeffbrown): Replace this with a proper pipeline.
  std::queue<input::InputEvent> pending_events_;

  std::vector<ViewHit> event_path_;
  uint64_t event_path_propagation_id_ = 0;

  // Occasionally a touch gesture gets lost because the hit test returns empty.
  // For those cases, we remember the pointer is "uncaptured" (identified by
  // device ID and pointer ID), and retry a hit test next time, in case we find
  // a target that can receive this gesture.
  std::set<std::pair<uint32_t, uint32_t>> uncaptured_pointers;

  fidl::Binding<input::InputDispatcher> binding_;

  std::unique_ptr<FocusChain> active_focus_chain_;

  fxl::WeakPtrFactory<InputDispatcherImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InputDispatcherImpl);
};

}  // namespace view_manager

#endif  // GARNET_BIN_UI_VIEW_MANAGER_INPUT_INPUT_DISPATCHER_IMPL_H_
