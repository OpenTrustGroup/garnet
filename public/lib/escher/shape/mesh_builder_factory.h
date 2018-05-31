// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_SHAPE_MESH_BUILDER_FACTORY_H_
#define LIB_ESCHER_SHAPE_MESH_BUILDER_FACTORY_H_

#include "lib/escher/forward_declarations.h"

namespace escher {

// Factory interface to obtain a MeshBuilder.
class MeshBuilderFactory {
 public:
  virtual ~MeshBuilderFactory() = default;

  virtual MeshBuilderPtr NewMeshBuilder(const MeshSpec& spec,
                                        size_t max_vertex_count,
                                        size_t max_index_count) = 0;
};

}  // namespace escher

#endif  // LIB_ESCHER_SHAPE_MESH_BUILDER_FACTORY_H_
