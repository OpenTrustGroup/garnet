// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_GUEST_VIEW_H_
#define GARNET_BIN_GUEST_GUEST_VIEW_H_

#include <zircon/types.h>

#include <fuchsia/cpp/views_v1.h>
#include "garnet/lib/machina/gpu_scanout.h"
#include "garnet/lib/machina/input_dispatcher.h"
#include "garnet/lib/machina/virtio_gpu.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/ui/scenic/client/host_memory.h"
#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/view_framework/base_view.h"

class GuestView;

class ScenicScanout : public machina::GpuScanout,
                      public views_v1::ViewProvider {
 public:
  static zx_status_t Create(component::ApplicationContext* application_context,
                            machina::InputDispatcher* input_dispatcher,
                            fbl::unique_ptr<GpuScanout>* out);

  ScenicScanout(component::ApplicationContext* application_context,
                machina::InputDispatcher* input_dispatcher);

  // |GpuScanout|
  void InvalidateRegion(const machina::GpuRect& rect) override;

  // |ViewProvider|
  void CreateView(
      fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<component::ServiceProvider> view_services)
      override;

 private:
  machina::InputDispatcher* input_dispatcher_;
  component::ApplicationContext* application_context_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  fidl::BindingSet<ViewProvider> bindings_;
  fbl::unique_ptr<GuestView> view_;
};

class GuestView : public mozart::BaseView {
 public:
  GuestView(
      machina::GpuScanout* scanout,
      machina::InputDispatcher* input_dispatcher,
      views_v1::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request);

  ~GuestView() override;

 private:
  // |BaseView|:
  void OnSceneInvalidated(images::PresentationInfo presentation_info) override;
  bool OnInputEvent(input::InputEvent event) override;

  scenic_lib::ShapeNode background_node_;
  scenic_lib::Material material_;
  images::ImageInfo image_info_;
  fbl::unique_ptr<scenic_lib::HostMemory> memory_;

  machina::InputDispatcher* input_dispatcher_;
  float previous_pointer_x_;
  float previous_pointer_y_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GuestView);
};

#endif  // GARNET_BIN_GUEST_GUEST_VIEW_H_
