// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_VIEW_TREE_IMPL_H_
#define GARNET_BIN_UI_VIEW_MANAGER_VIEW_TREE_IMPL_H_

#include "lib/ui/views/fidl/view_trees.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/macros.h"

namespace view_manager {

class ViewRegistry;
class ViewTreeState;

// ViewTree interface implementation.
// This object is owned by its associated ViewTreeState.
class ViewTreeImpl : public mozart::ViewTree,
                     public mozart::ViewContainer,
                     public app::ServiceProvider {
 public:
  ViewTreeImpl(ViewRegistry* registry, ViewTreeState* state);
  ~ViewTreeImpl() override;

 private:
  // |ViewTree|:
  void GetToken(const GetTokenCallback& callback) override;
  void GetServiceProvider(
      f1dl::InterfaceRequest<app::ServiceProvider> service_provider) override;
  void GetContainer(f1dl::InterfaceRequest<mozart::ViewContainer>
                        view_container_request) override;

  // |ViewContainer|:
  void SetListener(
      f1dl::InterfaceHandle<mozart::ViewContainerListener> listener) override;
  void AddChild(uint32_t child_key,
                f1dl::InterfaceHandle<mozart::ViewOwner> child_view_owner,
                zx::eventpair host_import_token) override;
  void RemoveChild(uint32_t child_key,
                   f1dl::InterfaceRequest<mozart::ViewOwner>
                       transferred_view_owner_request) override;
  void SetChildProperties(
      uint32_t child_key,
      mozart::ViewPropertiesPtr child_view_properties) override;
  void RequestFocus(uint32_t child_key) override;

  // |app::ServiceProvider|:
  void ConnectToService(const f1dl::String& service_name,
                        zx::channel client_handle) override;

  ViewRegistry* const registry_;
  ViewTreeState* const state_;
  f1dl::BindingSet<app::ServiceProvider> service_provider_bindings_;
  f1dl::BindingSet<mozart::ViewContainer> container_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewTreeImpl);
};

}  // namespace view_manager

#endif  // GARNET_BIN_UI_VIEW_MANAGER_VIEW_TREE_IMPL_H_
