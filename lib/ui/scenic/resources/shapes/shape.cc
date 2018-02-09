// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/resources/shapes/shape.h"

namespace scene_manager {

const ResourceTypeInfo Shape::kTypeInfo = {ResourceType::kShape, "Shape"};

Shape::Shape(Session* session,
             scenic::ResourceId id,
             const ResourceTypeInfo& type_info)
    : Resource(session, id, type_info) {
  FXL_DCHECK(type_info.IsKindOf(Shape::kTypeInfo));
}

}  // namespace scene_manager
