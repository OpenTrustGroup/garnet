// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_VIEW_TREE_IMPL_H_
#define GARNET_BIN_UI_VIEW_MANAGER_VIEW_TREE_IMPL_H_

#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"

namespace view_manager {

class ViewRegistry;
class ViewTreeState;

// ViewTree interface implementation.
// This object is owned by its associated ViewTreeState.
class ViewTreeImpl : public ::fuchsia::ui::viewsv1::ViewTree,
                     public ::fuchsia::ui::viewsv1::ViewContainer,
                     public fuchsia::sys::ServiceProvider {
 public:
  ViewTreeImpl(ViewRegistry* registry, ViewTreeState* state);
  ~ViewTreeImpl() override;

 private:
  // |ViewTree|:
  void GetToken(GetTokenCallback callback) override;
  void GetServiceProvider(fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>
                              service_provider) override;
  void GetContainer(
      fidl::InterfaceRequest<::fuchsia::ui::viewsv1::ViewContainer>
          view_container_request) override;

  // |ViewContainer|:
  void SetListener(
      fidl::InterfaceHandle<::fuchsia::ui::viewsv1::ViewContainerListener>
          listener) override;
  void AddChild(uint32_t child_key,
                fidl::InterfaceHandle<::fuchsia::ui::viewsv1token::ViewOwner>
                    child_view_owner,
                zx::eventpair host_import_token) override;
  void RemoveChild(
      uint32_t child_key,
      fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
          transferred_view_owner_request) override;
  void SetChildProperties(
      uint32_t child_key,
      ::fuchsia::ui::viewsv1::ViewPropertiesPtr child_view_properties) override;
  void RequestFocus(uint32_t child_key) override;
  void RequestSnapshotHACK(uint32_t child_key,
                           RequestSnapshotHACKCallback callback) override;

  // |fuchsia::sys::ServiceProvider|:
  void ConnectToService(fidl::StringPtr service_name,
                        zx::channel client_handle) override;

  ViewRegistry* const registry_;
  ViewTreeState* const state_;
  fidl::BindingSet<fuchsia::sys::ServiceProvider> service_provider_bindings_;
  fidl::BindingSet<::fuchsia::ui::viewsv1::ViewContainer> container_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewTreeImpl);
};

}  // namespace view_manager

#endif  // GARNET_BIN_UI_VIEW_MANAGER_VIEW_TREE_IMPL_H_
