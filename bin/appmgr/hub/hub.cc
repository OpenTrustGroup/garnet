// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/hub/hub.h"
#include "garnet/bin/appmgr/hub/hub_info.h"

#include <fbl/ref_ptr.h>
#include <fs/pseudo-file.h>
#include <fs/vnode.h>

namespace component {

Hub::Hub(fbl::RefPtr<fs::PseudoDir> root) : dir_(root) {}

zx_status_t Hub::AddEntry(fbl::String name, fbl::RefPtr<fs::Vnode> vn) {
  return dir_->AddEntry(fbl::move(name), fbl::move(vn));
}

zx_status_t Hub::AddEntry(fbl::String name, fbl::String value) {
  return dir_->AddEntry(fbl::move(name),
                        fbl::move(fbl::AdoptRef(new fs::UnbufferedPseudoFile(
                            [value](fbl::String* output) {
                              *output = value;
                              return ZX_OK;
                            }))));
}

zx_status_t Hub::CreateComponentDir() {
  component_dir_ = fbl::AdoptRef(new fs::PseudoDir());
  return AddEntry("c", component_dir_);
}

zx_status_t Hub::AddComponent(const HubInfo& hub_info) {
  if (!component_dir_) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  fbl::RefPtr<fs::PseudoDir> component_instance_dir;
  fbl::RefPtr<fs::Vnode> component_instance_vnode;
  zx_status_t status =
      component_dir_->Lookup(&component_instance_vnode, hub_info.label());
  if (status == ZX_ERR_NOT_FOUND) {
    component_instance_dir = fbl::AdoptRef(new fs::PseudoDir());
    component_dir_->AddEntry(hub_info.label(), component_instance_dir);
  } else {
    component_instance_dir = fbl::RefPtr<fs::PseudoDir>(
        static_cast<fs::PseudoDir*>(component_instance_vnode.get()));
  }
  return component_instance_dir->AddEntry(hub_info.koid(), hub_info.hub_dir());
}

zx_status_t Hub::RemoveComponent(const HubInfo& hub_info) {
  fbl::RefPtr<fs::Vnode> component_instance_vnode;
  zx_status_t status =
      component_dir_->Lookup(&component_instance_vnode, hub_info.label());
  if (status == ZX_OK) {
    auto component_instance_dir = fbl::RefPtr<fs::PseudoDir>(
        static_cast<fs::PseudoDir*>(component_instance_vnode.get()));
    status = component_instance_dir->RemoveEntry(hub_info.koid());
    if (component_instance_dir->IsEmpty()) {
      component_dir_->RemoveEntry(hub_info.label());
    }
    return status;
  }
  return ZX_ERR_NOT_FOUND;
}

}  // namespace component
