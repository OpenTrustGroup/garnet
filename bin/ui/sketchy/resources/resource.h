// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SKETCHY_RESOURCES_RESOURCE_H_
#define GARNET_BIN_UI_SKETCHY_RESOURCES_RESOURCE_H_

#include "garnet/bin/ui/sketchy/resources/types.h"
#include "lib/escher/base/typed_reffable.h"
#include "lib/fxl/memory/ref_counted.h"

namespace sketchy_service {

class Resource : public escher::TypedReffable<ResourceTypeInfo> {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  // For the given resource type info, returns the resource that will act as
  // the target for commands directed at this resource. Subclasses (notably the
  // |Import| since their binding are not mutable) may return alternate
  // resources to act as the recipients of commands.
  Resource* GetDelegate(const ResourceTypeInfo& type_info);

 protected:
  Resource() {}

  FXL_DISALLOW_COPY_AND_ASSIGN(Resource);
};

using ResourcePtr = fxl::RefPtr<Resource>;

}  // namespace sketchy_service

#endif  // GARNET_BIN_UI_SKETCHY_RESOURCES_RESOURCE_H_
