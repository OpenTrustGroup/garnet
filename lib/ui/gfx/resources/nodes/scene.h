// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_NODES_SCENE_H_
#define GARNET_LIB_UI_GFX_RESOURCES_NODES_SCENE_H_

#include "garnet/lib/ui/gfx/resources/nodes/node.h"
#include "lib/fxl/macros.h"

namespace scenic {
namespace gfx {

class AmbientLight;
class DirectionalLight;
class Light;
class Scene;
using AmbientLightPtr = fxl::RefPtr<AmbientLight>;
using DirectionalLightPtr = fxl::RefPtr<DirectionalLight>;
using LightPtr = fxl::RefPtr<Light>;
using ScenePtr = fxl::RefPtr<Scene>;

class Scene final : public Node {
 public:
  static const ResourceTypeInfo kTypeInfo;

  Scene(Session* session, scenic::ResourceId node_id);
  ~Scene() override;

  bool AddLight(const LightPtr& light);

  // |Resource|.
  void Accept(class ResourceVisitor* visitor) override;

  // |Resource|.
  bool Detach() override;

  const std::vector<AmbientLightPtr>& ambient_lights() const {
    return ambient_lights_;
  }

  const std::vector<DirectionalLightPtr>& directional_lights() const {
    return directional_lights_;
  }

 private:
  std::vector<AmbientLightPtr> ambient_lights_;
  std::vector<DirectionalLightPtr> directional_lights_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Scene);
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_NODES_SCENE_H_
