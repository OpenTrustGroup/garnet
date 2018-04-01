// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/session.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <trace/event.h>
#include <zx/time.h>
#include <utility>

#include <fuchsia/cpp/gfx.h>
#include "garnet/lib/ui/gfx/engine/hit_tester.h"
#include "garnet/lib/ui/gfx/engine/session_handler.h"
#include "garnet/lib/ui/gfx/resources/buffer.h"
#include "garnet/lib/ui/gfx/resources/camera.h"
#include "garnet/lib/ui/gfx/resources/compositor/display_compositor.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer_stack.h"
#include "garnet/lib/ui/gfx/resources/gpu_memory.h"
#include "garnet/lib/ui/gfx/resources/host_memory.h"
#include "garnet/lib/ui/gfx/resources/image.h"
#include "garnet/lib/ui/gfx/resources/image_pipe.h"
#include "garnet/lib/ui/gfx/resources/image_pipe_handler.h"
#include "garnet/lib/ui/gfx/resources/lights/ambient_light.h"
#include "garnet/lib/ui/gfx/resources/lights/directional_light.h"
#include "garnet/lib/ui/gfx/resources/nodes/entity_node.h"
#include "garnet/lib/ui/gfx/resources/nodes/node.h"
#include "garnet/lib/ui/gfx/resources/nodes/scene.h"
#include "garnet/lib/ui/gfx/resources/nodes/shape_node.h"
#include "garnet/lib/ui/gfx/resources/renderers/renderer.h"
#include "garnet/lib/ui/gfx/resources/shapes/circle_shape.h"
#include "garnet/lib/ui/gfx/resources/shapes/mesh_shape.h"
#include "garnet/lib/ui/gfx/resources/shapes/rectangle_shape.h"
#include "garnet/lib/ui/gfx/resources/shapes/rounded_rectangle_shape.h"
#include "garnet/lib/ui/gfx/resources/stereo_camera.h"
#include "garnet/lib/ui/gfx/resources/variable.h"
#include "garnet/lib/ui/gfx/util/unwrap.h"
#include "garnet/lib/ui/gfx/util/wrap.h"

#include "lib/escher/hmd/pose_buffer.h"
#include "lib/escher/shape/mesh.h"
#include "lib/escher/shape/rounded_rect_factory.h"
#include "lib/escher/util/type_utils.h"

namespace scenic {
namespace gfx {

namespace {

// Makes it convenient to check that a value is constant and of a specific type,
// or a variable.
// TODO: There should also be a convenient way of type-checking a variable;
// this will necessarily involve looking up the value in the ResourceMap.
constexpr std::array<::gfx::Value::Tag, 2> kFloatValueTypes{
    {::gfx::Value::Tag::kVector1, ::gfx::Value::Tag::kVariableId}};

// Converts the provided vector of scene_manager hits into a fidl array of
// HitPtrs.
fidl::VectorPtr<::gfx::Hit> WrapHits(const std::vector<Hit>& hits) {
  fidl::VectorPtr<::gfx::Hit> wrapped_hits;
  wrapped_hits.resize(hits.size());
  for (size_t i = 0; i < hits.size(); ++i) {
    const Hit& hit = hits[i];
    ::gfx::Hit wrapped_hit;
    wrapped_hit.tag_value = hit.tag_value;
    wrapped_hit.ray_origin = Wrap(hit.ray.origin);
    wrapped_hit.ray_direction = Wrap(hit.ray.direction);
    wrapped_hit.inverse_transform = Wrap(hit.inverse_transform);
    wrapped_hit.distance = hit.distance;
    wrapped_hits->at(i) = std::move(wrapped_hit);
  }
  return wrapped_hits;
}

}  // anonymous namespace

Session::Session(SessionId id,
                 Engine* engine,
                 EventReporter* event_reporter,
                 ErrorReporter* error_reporter)
    : id_(id),
      engine_(engine),
      error_reporter_(error_reporter),
      event_reporter_(event_reporter),
      resources_(error_reporter),
      weak_factory_(this) {
  FXL_DCHECK(engine);
  FXL_DCHECK(error_reporter);
}

Session::~Session() {
  FXL_DCHECK(!is_valid_);
}

bool Session::ApplyCommand(::gfx::Command command) {
  switch (command.Which()) {
    case ::gfx::Command::Tag::kCreateResource:
      return ApplyCreateResourceCommand(std::move(command.create_resource()));
    case ::gfx::Command::Tag::kReleaseResource:
      return ApplyReleaseResourceCommand(std::move(command.release_resource()));
    case ::gfx::Command::Tag::kExportResource:
      return ApplyExportResourceCommand(std::move(command.export_resource()));
    case ::gfx::Command::Tag::kImportResource:
      return ApplyImportResourceCommand(std::move(command.import_resource()));
    case ::gfx::Command::Tag::kAddChild:
      return ApplyAddChildCommand(std::move(command.add_child()));
    case ::gfx::Command::Tag::kAddPart:
      return ApplyAddPartCommand(std::move(command.add_part()));
    case ::gfx::Command::Tag::kDetach:
      return ApplyDetachCommand(std::move(command.detach()));
    case ::gfx::Command::Tag::kDetachChildren:
      return ApplyDetachChildrenCommand(std::move(command.detach_children()));
    case ::gfx::Command::Tag::kSetTag:
      return ApplySetTagCommand(std::move(command.set_tag()));
    case ::gfx::Command::Tag::kSetTranslation:
      return ApplySetTranslationCommand(std::move(command.set_translation()));
    case ::gfx::Command::Tag::kSetScale:
      return ApplySetScaleCommand(std::move(command.set_scale()));
    case ::gfx::Command::Tag::kSetRotation:
      return ApplySetRotationCommand(std::move(command.set_rotation()));
    case ::gfx::Command::Tag::kSetAnchor:
      return ApplySetAnchorCommand(std::move(command.set_anchor()));
    case ::gfx::Command::Tag::kSetSize:
      return ApplySetSizeCommand(std::move(command.set_size()));
    case ::gfx::Command::Tag::kSetShape:
      return ApplySetShapeCommand(std::move(command.set_shape()));
    case ::gfx::Command::Tag::kSetMaterial:
      return ApplySetMaterialCommand(std::move(command.set_material()));
    case ::gfx::Command::Tag::kSetClip:
      return ApplySetClipCommand(std::move(command.set_clip()));
    case ::gfx::Command::Tag::kSetHitTestBehavior:
      return ApplySetHitTestBehaviorCommand(
          std::move(command.set_hit_test_behavior()));
    case ::gfx::Command::Tag::kSetCamera:
      return ApplySetCameraCommand(std::move(command.set_camera()));
    case ::gfx::Command::Tag::kSetCameraTransform:
      return ApplySetCameraTransformCommand(
          std::move(command.set_camera_transform()));
    case ::gfx::Command::Tag::kSetCameraProjection:
      return ApplySetCameraProjectionCommand(
          std::move(command.set_camera_projection()));
    case ::gfx::Command::Tag::kSetStereoCameraProjection:
      return ApplySetStereoCameraProjectionCommand(
          std::move(command.set_stereo_camera_projection()));
    case ::gfx::Command::Tag::kSetCameraPoseBuffer:
      return ApplySetCameraPoseBufferCommand(
          std::move(command.set_camera_pose_buffer()));
    case ::gfx::Command::Tag::kSetLightColor:
      return ApplySetLightColorCommand(std::move(command.set_light_color()));
    case ::gfx::Command::Tag::kSetLightDirection:
      return ApplySetLightDirectionCommand(
          std::move(command.set_light_direction()));
    case ::gfx::Command::Tag::kAddLight:
      return ApplyAddLightCommand(std::move(command.add_light()));
    case ::gfx::Command::Tag::kDetachLight:
      return ApplyDetachLightCommand(std::move(command.detach_light()));
    case ::gfx::Command::Tag::kDetachLights:
      return ApplyDetachLightsCommand(std::move(command.detach_lights()));
    case ::gfx::Command::Tag::kSetTexture:
      return ApplySetTextureCommand(std::move(command.set_texture()));
    case ::gfx::Command::Tag::kSetColor:
      return ApplySetColorCommand(std::move(command.set_color()));
    case ::gfx::Command::Tag::kBindMeshBuffers:
      return ApplyBindMeshBuffersCommand(
          std::move(command.bind_mesh_buffers()));
    case ::gfx::Command::Tag::kAddLayer:
      return ApplyAddLayerCommand(std::move(command.add_layer()));
    case ::gfx::Command::Tag::kSetLayerStack:
      return ApplySetLayerStackCommand(std::move(command.set_layer_stack()));
    case ::gfx::Command::Tag::kSetRenderer:
      return ApplySetRendererCommand(std::move(command.set_renderer()));
    case ::gfx::Command::Tag::kSetRendererParam:
      return ApplySetRendererParamCommand(
          std::move(command.set_renderer_param()));
    case ::gfx::Command::Tag::kSetEventMask:
      return ApplySetEventMaskCommand(std::move(command.set_event_mask()));
    case ::gfx::Command::Tag::kSetLabel:
      return ApplySetLabelCommand(std::move(command.set_label()));
    case ::gfx::Command::Tag::kSetDisableClipping:
      return ApplySetDisableClippingCommand(
          std::move(command.set_disable_clipping()));
    case ::gfx::Command::Tag::Invalid:
      // FIDL validation should make this impossible.
      FXL_CHECK(false);
      return false;
  }
}

bool Session::ApplyCreateResourceCommand(::gfx::CreateResourceCommand command) {
  const scenic::ResourceId id = command.id;
  if (id == 0) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplyCreateResourceCommand(): invalid ID: "
        << command;
    return false;
  }

  switch (command.resource.Which()) {
    case ::gfx::ResourceArgs::Tag::kMemory:
      return ApplyCreateMemory(id, std::move(command.resource.memory()));
    case ::gfx::ResourceArgs::Tag::kImage:
      return ApplyCreateImage(id, std::move(command.resource.image()));
    case ::gfx::ResourceArgs::Tag::kImagePipe:
      return ApplyCreateImagePipe(id, std::move(command.resource.image_pipe()));
    case ::gfx::ResourceArgs::Tag::kBuffer:
      return ApplyCreateBuffer(id, std::move(command.resource.buffer()));
    case ::gfx::ResourceArgs::Tag::kScene:
      return ApplyCreateScene(id, std::move(command.resource.scene()));
    case ::gfx::ResourceArgs::Tag::kCamera:
      return ApplyCreateCamera(id, std::move(command.resource.camera()));
    case ::gfx::ResourceArgs::Tag::kStereoCamera:
      return ApplyCreateStereoCamera(
          id, std::move(command.resource.stereo_camera()));
    case ::gfx::ResourceArgs::Tag::kRenderer:
      return ApplyCreateRenderer(id, std::move(command.resource.renderer()));
    case ::gfx::ResourceArgs::Tag::kAmbientLight:
      return ApplyCreateAmbientLight(
          id, std::move(command.resource.ambient_light()));
    case ::gfx::ResourceArgs::Tag::kDirectionalLight:
      return ApplyCreateDirectionalLight(
          id, std::move(command.resource.directional_light()));
    case ::gfx::ResourceArgs::Tag::kRectangle:
      return ApplyCreateRectangle(id, std::move(command.resource.rectangle()));
    case ::gfx::ResourceArgs::Tag::kRoundedRectangle:
      return ApplyCreateRoundedRectangle(
          id, std::move(command.resource.rounded_rectangle()));
    case ::gfx::ResourceArgs::Tag::kCircle:
      return ApplyCreateCircle(id, std::move(command.resource.circle()));
    case ::gfx::ResourceArgs::Tag::kMesh:
      return ApplyCreateMesh(id, std::move(command.resource.mesh()));
    case ::gfx::ResourceArgs::Tag::kMaterial:
      return ApplyCreateMaterial(id, std::move(command.resource.material()));
    case ::gfx::ResourceArgs::Tag::kClipNode:
      return ApplyCreateClipNode(id, std::move(command.resource.clip_node()));
    case ::gfx::ResourceArgs::Tag::kEntityNode:
      return ApplyCreateEntityNode(id,
                                   std::move(command.resource.entity_node()));
    case ::gfx::ResourceArgs::Tag::kShapeNode:
      return ApplyCreateShapeNode(id, std::move(command.resource.shape_node()));
    case ::gfx::ResourceArgs::Tag::kDisplayCompositor:
      return ApplyCreateDisplayCompositor(
          id, std::move(command.resource.display_compositor()));
    case ::gfx::ResourceArgs::Tag::kImagePipeCompositor:
      return ApplyCreateImagePipeCompositor(
          id, std::move(command.resource.image_pipe_compositor()));
    case ::gfx::ResourceArgs::Tag::kLayerStack:
      return ApplyCreateLayerStack(id,
                                   std::move(command.resource.layer_stack()));
    case ::gfx::ResourceArgs::Tag::kLayer:
      return ApplyCreateLayer(id, std::move(command.resource.layer()));
    case ::gfx::ResourceArgs::Tag::kVariable:
      return ApplyCreateVariable(id, std::move(command.resource.variable()));
    case ::gfx::ResourceArgs::Tag::Invalid:
      // FIDL validation should make this impossible.
      FXL_CHECK(false);
      return false;
  }
}

bool Session::ApplyReleaseResourceCommand(
    ::gfx::ReleaseResourceCommand command) {
  return resources_.RemoveResource(command.id);
}

bool Session::ApplyExportResourceCommand(::gfx::ExportResourceCommand command) {
  if (!command.token) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplyExportResourceCommand(): "
           "no token provided.";
    return false;
  }
  if (auto resource = resources_.FindResource<Resource>(command.id)) {
    return engine_->resource_linker()->ExportResource(resource.get(),
                                                      std::move(command.token));
  }
  return false;
}

bool Session::ApplyImportResourceCommand(::gfx::ImportResourceCommand command) {
  if (!command.token) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplyImportResourceCommand(): "
           "no token provided.";
    return false;
  }
  ImportPtr import =
      fxl::MakeRefCounted<Import>(this, command.id, command.spec);
  return engine_->resource_linker()->ImportResource(import.get(), command.spec,
                                                    std::move(command.token)) &&
         resources_.AddResource(command.id, std::move(import));
}

bool Session::ApplyAddChildCommand(::gfx::AddChildCommand command) {
  // Find the parent and child nodes.
  if (auto parent_node = resources_.FindResource<Node>(command.node_id)) {
    if (auto child_node = resources_.FindResource<Node>(command.child_id)) {
      return parent_node->AddChild(std::move(child_node));
    }
  }
  return false;
}

bool Session::ApplyAddPartCommand(::gfx::AddPartCommand command) {
  // Find the parent and part nodes.
  if (auto parent_node = resources_.FindResource<Node>(command.node_id)) {
    if (auto part_node = resources_.FindResource<Node>(command.part_id)) {
      return parent_node->AddPart(std::move(part_node));
    }
  }
  return false;
}

bool Session::ApplyDetachCommand(::gfx::DetachCommand command) {
  if (auto resource = resources_.FindResource<Resource>(command.id)) {
    return resource->Detach();
  }
  return false;
}

bool Session::ApplyDetachChildrenCommand(::gfx::DetachChildrenCommand command) {
  if (auto node = resources_.FindResource<Node>(command.node_id)) {
    return node->DetachChildren();
  }
  return false;
}

bool Session::ApplySetTagCommand(::gfx::SetTagCommand command) {
  if (auto node = resources_.FindResource<Node>(command.node_id)) {
    return node->SetTagValue(command.tag_value);
  }
  return false;
}

bool Session::ApplySetTranslationCommand(::gfx::SetTranslationCommand command) {
  if (auto node = resources_.FindResource<Node>(command.id)) {
    if (IsVariable(command.value)) {
      if (auto variable = resources_.FindVariableResource<Vector3Variable>(
              command.value.variable_id)) {
        return node->SetTranslation(variable);
      }
    } else {
      return node->SetTranslation(UnwrapVector3(command.value));
    }
  }
  return false;
}

bool Session::ApplySetScaleCommand(::gfx::SetScaleCommand command) {
  if (auto node = resources_.FindResource<Node>(command.id)) {
    if (IsVariable(command.value)) {
      if (auto variable = resources_.FindVariableResource<Vector3Variable>(
              command.value.variable_id)) {
        return node->SetScale(variable);
      }
    } else {
      return node->SetScale(UnwrapVector3(command.value));
    }
  }
  return false;
}

bool Session::ApplySetRotationCommand(::gfx::SetRotationCommand command) {
  if (auto node = resources_.FindResource<Node>(command.id)) {
    if (IsVariable(command.value)) {
      if (auto variable = resources_.FindVariableResource<QuaternionVariable>(
              command.value.variable_id)) {
        return node->SetRotation(variable);
      }
    } else {
      return node->SetRotation(UnwrapQuaternion(command.value));
    }
  }
  return false;
}

bool Session::ApplySetAnchorCommand(::gfx::SetAnchorCommand command) {
  if (auto node = resources_.FindResource<Node>(command.id)) {
    if (IsVariable(command.value)) {
      if (auto variable = resources_.FindVariableResource<Vector3Variable>(
              command.value.variable_id)) {
        return node->SetAnchor(variable);
      }
    }
    return node->SetAnchor(UnwrapVector3(command.value));
  }
  return false;
}

bool Session::ApplySetSizeCommand(::gfx::SetSizeCommand command) {
  if (auto layer = resources_.FindResource<Layer>(command.id)) {
    if (IsVariable(command.value)) {
      error_reporter_->ERROR()
          << "scenic::gfx::Session::ApplySetSizeCommand(): "
             "unimplemented for variable value.";
      return false;
    }
    return layer->SetSize(UnwrapVector2(command.value));
  }
  return false;
}

bool Session::ApplySetShapeCommand(::gfx::SetShapeCommand command) {
  if (auto node = resources_.FindResource<ShapeNode>(command.node_id)) {
    if (auto shape = resources_.FindResource<Shape>(command.shape_id)) {
      node->SetShape(std::move(shape));
      return true;
    }
  }
  return false;
}

bool Session::ApplySetMaterialCommand(::gfx::SetMaterialCommand command) {
  if (auto node = resources_.FindResource<ShapeNode>(command.node_id)) {
    if (auto material =
            resources_.FindResource<Material>(command.material_id)) {
      node->SetMaterial(std::move(material));
      return true;
    }
  }
  return false;
}

bool Session::ApplySetClipCommand(::gfx::SetClipCommand command) {
  if (command.clip_id != 0) {
    // TODO(MZ-167): Support non-zero clip_id.
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplySetClipCommand(): only "
           "clip_to_self is implemented.";
    return false;
  }

  if (auto node = resources_.FindResource<Node>(command.node_id)) {
    return node->SetClipToSelf(command.clip_to_self);
  }

  return false;
}

bool Session::ApplySetHitTestBehaviorCommand(
    ::gfx::SetHitTestBehaviorCommand command) {
  if (auto node = resources_.FindResource<Node>(command.node_id)) {
    return node->SetHitTestBehavior(command.hit_test_behavior);
  }

  return false;
}

bool Session::ApplySetCameraCommand(::gfx::SetCameraCommand command) {
  if (auto renderer = resources_.FindResource<Renderer>(command.renderer_id)) {
    if (command.camera_id == 0) {
      renderer->SetCamera(nullptr);
      return true;
    } else if (auto camera =
                   resources_.FindResource<Camera>(command.camera_id)) {
      renderer->SetCamera(std::move(camera));
      return true;
    }
  }
  return false;
}

bool Session::ApplySetTextureCommand(::gfx::SetTextureCommand command) {
  if (auto material = resources_.FindResource<Material>(command.material_id)) {
    if (command.texture_id == 0) {
      material->SetTexture(nullptr);
      return true;
    } else if (auto image =
                   resources_.FindResource<ImageBase>(command.texture_id)) {
      material->SetTexture(std::move(image));
      return true;
    }
  }
  return false;
}

bool Session::ApplySetColorCommand(::gfx::SetColorCommand command) {
  if (auto material = resources_.FindResource<Material>(command.material_id)) {
    if (IsVariable(command.color)) {
      error_reporter_->ERROR()
          << "scenic::gfx::Session::ApplySetColorCommand(): "
             "unimplemented for variable color.";
      return false;
    }

    auto& color = command.color.value;
    float red = static_cast<float>(color.red) / 255.f;
    float green = static_cast<float>(color.green) / 255.f;
    float blue = static_cast<float>(color.blue) / 255.f;
    float alpha = static_cast<float>(color.alpha) / 255.f;
    material->SetColor(red, green, blue, alpha);
    return true;
  }
  return false;
}

bool Session::ApplyBindMeshBuffersCommand(
    ::gfx::BindMeshBuffersCommand command) {
  auto mesh = resources_.FindResource<MeshShape>(command.mesh_id);
  auto index_buffer = resources_.FindResource<Buffer>(command.index_buffer_id);
  auto vertex_buffer =
      resources_.FindResource<Buffer>(command.vertex_buffer_id);
  if (mesh && index_buffer && vertex_buffer) {
    return mesh->BindBuffers(std::move(index_buffer), command.index_format,
                             command.index_offset, command.index_count,
                             std::move(vertex_buffer), command.vertex_format,
                             command.vertex_offset, command.vertex_count,
                             Unwrap(command.bounding_box));
  }
  return false;
}

bool Session::ApplyAddLayerCommand(::gfx::AddLayerCommand command) {
  auto layer_stack =
      resources_.FindResource<LayerStack>(command.layer_stack_id);
  auto layer = resources_.FindResource<Layer>(command.layer_id);
  if (layer_stack && layer) {
    return layer_stack->AddLayer(std::move(layer));
  }
  return false;
}

bool Session::ApplySetLayerStackCommand(::gfx::SetLayerStackCommand command) {
  auto compositor = resources_.FindResource<Compositor>(command.compositor_id);
  auto layer_stack =
      resources_.FindResource<LayerStack>(command.layer_stack_id);
  if (compositor && layer_stack) {
    return compositor->SetLayerStack(std::move(layer_stack));
  }
  return false;
}

bool Session::ApplySetRendererCommand(::gfx::SetRendererCommand command) {
  auto layer = resources_.FindResource<Layer>(command.layer_id);
  auto renderer = resources_.FindResource<Renderer>(command.renderer_id);

  if (layer && renderer) {
    return layer->SetRenderer(std::move(renderer));
  }
  return false;
}

bool Session::ApplySetRendererParamCommand(
    ::gfx::SetRendererParamCommand command) {
  auto renderer = resources_.FindResource<Renderer>(command.renderer_id);
  if (renderer) {
    switch (command.param.Which()) {
      case ::gfx::RendererParam::Tag::kShadowTechnique:
        return renderer->SetShadowTechnique(command.param.shadow_technique());
      case ::gfx::RendererParam::Tag::Invalid:
        error_reporter_->ERROR()
            << "scenic::gfx::Session::ApplySetRendererParamCommand(): "
               "invalid param.";
    }
  }
  return false;
}

bool Session::ApplySetEventMaskCommand(::gfx::SetEventMaskCommand command) {
  if (auto r = resources_.FindResource<Resource>(command.id)) {
    return r->SetEventMask(command.event_mask);
  }
  return false;
}

bool Session::ApplySetCameraTransformCommand(
    ::gfx::SetCameraTransformCommand command) {
  // TODO(MZ-123): support variables.
  if (IsVariable(command.eye_position) || IsVariable(command.eye_look_at) ||
      IsVariable(command.eye_up)) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplySetCameraTransformCommand(): "
           "unimplemented: variable properties.";
    return false;
  } else if (auto camera = resources_.FindResource<Camera>(command.camera_id)) {
    camera->SetTransform(UnwrapVector3(command.eye_position),
                         UnwrapVector3(command.eye_look_at),
                         UnwrapVector3(command.eye_up));
    return true;
  }
  return false;
}

bool Session::ApplySetCameraProjectionCommand(
    ::gfx::SetCameraProjectionCommand command) {
  // TODO(MZ-123): support variables.
  if (IsVariable(command.fovy)) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplySetCameraProjectionCommand(): "
           "unimplemented: variable properties.";
    return false;
  } else if (auto camera = resources_.FindResource<Camera>(command.camera_id)) {
    camera->SetProjection(UnwrapFloat(command.fovy));
    return true;
  }
  return false;
}

bool Session::ApplySetStereoCameraProjectionCommand(
    ::gfx::SetStereoCameraProjectionCommand command) {
  if (IsVariable(command.left_projection) ||
      IsVariable(command.right_projection)) {
    error_reporter_->ERROR()
        << "scene_manager::Session::ApplySetStereoCameraProjectionOp(): "
           "unimplemented: variable properties.";
    return false;
  } else if (auto stereo_camera =
                 resources_.FindResource<StereoCamera>(command.camera_id)) {
    stereo_camera->SetStereoProjection(Unwrap(command.left_projection.value),
                                       Unwrap(command.right_projection.value));
    return true;
  }
  return false;
}

bool Session::ApplySetCameraPoseBufferCommand(
    ::gfx::SetCameraPoseBufferCommand command) {
  if (command.base_time > zx::clock::get(ZX_CLOCK_MONOTONIC).get()) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplySetCameraPoseBufferCommand(): "
           "base time not in the past";
    return false;
  }

  auto buffer = resources_.FindResource<Buffer>(command.buffer_id);
  if (!buffer) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplySetCameraPoseBufferCommand(): "
           "invalid buffer ID";
    return false;
  }

  if (command.num_entries < 1) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplySetCameraPoseBufferCommand(): "
           "must have at least one entry in the pose buffer";
    return false;
  }

  if (buffer->size() < command.num_entries * sizeof(escher::hmd::Pose)) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplySetCameraPoseBufferCommand(): "
           "buffer is not large enough";
    return false;
  }

  auto camera = resources_.FindResource<Camera>(command.camera_id);
  if (!camera) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplySetCameraPoseBufferCommand(): "
           "invalid camera ID";
    return false;
  }

  camera->SetPoseBuffer(buffer, command.num_entries, command.base_time,
                        command.time_interval);

  return true;
}

bool Session::ApplySetLightColorCommand(::gfx::SetLightColorCommand command) {
  // TODO(MZ-123): support variables.
  if (command.color.variable_id) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplySetLightColorCommand(): "
           "unimplemented: variable color.";
    return false;
  } else if (auto light = resources_.FindResource<Light>(command.light_id)) {
    return light->SetColor(Unwrap(command.color.value));
  }
  return false;
}

bool Session::ApplySetLightDirectionCommand(
    ::gfx::SetLightDirectionCommand command) {
  // TODO(MZ-123): support variables.
  if (command.direction.variable_id) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplySetLightDirectionCommand(): "
           "unimplemented: variable direction.";
    return false;
  } else if (auto light =
                 resources_.FindResource<DirectionalLight>(command.light_id)) {
    return light->SetDirection(Unwrap(command.direction.value));
  }
  return false;
}

bool Session::ApplyAddLightCommand(::gfx::AddLightCommand command) {
  if (auto scene = resources_.FindResource<Scene>(command.scene_id)) {
    if (auto light = resources_.FindResource<Light>(command.light_id)) {
      return scene->AddLight(std::move(light));
    }
  }

  error_reporter_->ERROR()
      << "scenic::gfx::Session::ApplyAddLightCommand(): unimplemented.";
  return false;
}

bool Session::ApplyDetachLightCommand(::gfx::DetachLightCommand command) {
  error_reporter_->ERROR()
      << "scenic::gfx::Session::ApplyDetachLightCommand(): unimplemented.";
  return false;
}

bool Session::ApplyDetachLightsCommand(::gfx::DetachLightsCommand command) {
  error_reporter_->ERROR()
      << "scenic::gfx::Session::ApplyDetachLightsCommand(): unimplemented.";
  return false;
}

bool Session::ApplySetLabelCommand(::gfx::SetLabelCommand command) {
  if (auto r = resources_.FindResource<Resource>(command.id)) {
    return r->SetLabel(command.label.get());
  }
  return false;
}

bool Session::ApplySetDisableClippingCommand(
    ::gfx::SetDisableClippingCommand command) {
  if (auto r = resources_.FindResource<Renderer>(command.renderer_id)) {
    r->DisableClipping(command.disable_clipping);
    return true;
  }
  return false;
}

bool Session::ApplyCreateMemory(scenic::ResourceId id, ::gfx::MemoryArgs args) {
  auto memory = CreateMemory(id, std::move(args));
  return memory ? resources_.AddResource(id, std::move(memory)) : false;
}

bool Session::ApplyCreateImage(scenic::ResourceId id, ::gfx::ImageArgs args) {
  if (auto memory = resources_.FindResource<Memory>(args.memory_id)) {
    if (auto image = CreateImage(id, std::move(memory), args)) {
      return resources_.AddResource(id, std::move(image));
    }
  }

  return false;
}

bool Session::ApplyCreateImagePipe(scenic::ResourceId id,
                                   ::gfx::ImagePipeArgs args) {
  auto image_pipe = fxl::MakeRefCounted<ImagePipe>(
      this, id, std::move(args.image_pipe_request));
  return resources_.AddResource(id, image_pipe);
}

bool Session::ApplyCreateBuffer(scenic::ResourceId id, ::gfx::BufferArgs args) {
  if (auto memory = resources_.FindResource<Memory>(args.memory_id)) {
    if (auto buffer = CreateBuffer(id, std::move(memory), args.memory_offset,
                                   args.num_bytes)) {
      return resources_.AddResource(id, std::move(buffer));
    }
  }
  return false;
}

bool Session::ApplyCreateScene(scenic::ResourceId id, ::gfx::SceneArgs args) {
  auto scene = CreateScene(id, std::move(args));
  return scene ? resources_.AddResource(id, std::move(scene)) : false;
}

bool Session::ApplyCreateCamera(scenic::ResourceId id, ::gfx::CameraArgs args) {
  auto camera = CreateCamera(id, std::move(args));
  return camera ? resources_.AddResource(id, std::move(camera)) : false;
}

bool Session::ApplyCreateStereoCamera(scenic::ResourceId id,
                                      ::gfx::StereoCameraArgs args) {
  auto camera = CreateStereoCamera(id, args);
  return camera ? resources_.AddResource(id, std::move(camera)) : false;
}

bool Session::ApplyCreateRenderer(scenic::ResourceId id,
                                  ::gfx::RendererArgs args) {
  auto renderer = CreateRenderer(id, std::move(args));
  return renderer ? resources_.AddResource(id, std::move(renderer)) : false;
}

bool Session::ApplyCreateAmbientLight(scenic::ResourceId id,
                                      ::gfx::AmbientLightArgs args) {
  auto light = CreateAmbientLight(id);
  return light ? resources_.AddResource(id, std::move(light)) : false;
}

bool Session::ApplyCreateDirectionalLight(scenic::ResourceId id,
                                          ::gfx::DirectionalLightArgs args) {
  auto light = CreateDirectionalLight(id);
  return light ? resources_.AddResource(id, std::move(light)) : false;
}

bool Session::ApplyCreateRectangle(scenic::ResourceId id,
                                   ::gfx::RectangleArgs args) {
  if (!AssertValueIsOfType(args.width, kFloatValueTypes) ||
      !AssertValueIsOfType(args.height, kFloatValueTypes)) {
    return false;
  }

  // TODO(MZ-123): support variables.
  if (IsVariable(args.width) || IsVariable(args.height)) {
    error_reporter_->ERROR() << "scenic::gfx::Session::ApplyCreateRectangle(): "
                                "unimplemented: variable width/height.";
    return false;
  }

  auto rectangle =
      CreateRectangle(id, args.width.vector1(), args.height.vector1());
  return rectangle ? resources_.AddResource(id, std::move(rectangle)) : false;
}

bool Session::ApplyCreateRoundedRectangle(scenic::ResourceId id,
                                          ::gfx::RoundedRectangleArgs args) {
  if (!AssertValueIsOfType(args.width, kFloatValueTypes) ||
      !AssertValueIsOfType(args.height, kFloatValueTypes) ||
      !AssertValueIsOfType(args.top_left_radius, kFloatValueTypes) ||
      !AssertValueIsOfType(args.top_right_radius, kFloatValueTypes) ||
      !AssertValueIsOfType(args.bottom_left_radius, kFloatValueTypes) ||
      !AssertValueIsOfType(args.bottom_right_radius, kFloatValueTypes)) {
    return false;
  }

  // TODO(MZ-123): support variables.
  if (IsVariable(args.width) || IsVariable(args.height) ||
      IsVariable(args.top_left_radius) || IsVariable(args.top_right_radius) ||
      IsVariable(args.bottom_left_radius) ||
      IsVariable(args.bottom_right_radius)) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplyCreateRoundedRectangle(): "
           "unimplemented: variable width/height/radii.";
    return false;
  }

  const float width = args.width.vector1();
  const float height = args.height.vector1();
  const float top_left_radius = args.top_left_radius.vector1();
  const float top_right_radius = args.top_right_radius.vector1();
  const float bottom_right_radius = args.bottom_right_radius.vector1();
  const float bottom_left_radius = args.bottom_left_radius.vector1();

  auto rectangle = CreateRoundedRectangle(id, width, height, top_left_radius,
                                          top_right_radius, bottom_right_radius,
                                          bottom_left_radius);
  return rectangle ? resources_.AddResource(id, std::move(rectangle)) : false;
}

bool Session::ApplyCreateCircle(scenic::ResourceId id, ::gfx::CircleArgs args) {
  if (!AssertValueIsOfType(args.radius, kFloatValueTypes)) {
    return false;
  }

  // TODO(MZ-123): support variables.
  if (IsVariable(args.radius)) {
    error_reporter_->ERROR() << "scenic::gfx::Session::ApplyCreateCircle(): "
                                "unimplemented: variable radius.";
    return false;
  }

  auto circle = CreateCircle(id, args.radius.vector1());
  return circle ? resources_.AddResource(id, std::move(circle)) : false;
}

bool Session::ApplyCreateMesh(scenic::ResourceId id, ::gfx::MeshArgs args) {
  auto mesh = CreateMesh(id);
  return mesh ? resources_.AddResource(id, std::move(mesh)) : false;
}

bool Session::ApplyCreateMaterial(scenic::ResourceId id,
                                  ::gfx::MaterialArgs args) {
  auto material = CreateMaterial(id);
  return material ? resources_.AddResource(id, std::move(material)) : false;
}

bool Session::ApplyCreateClipNode(scenic::ResourceId id,
                                  ::gfx::ClipNodeArgs args) {
  auto node = CreateClipNode(id, std::move(args));
  return node ? resources_.AddResource(id, std::move(node)) : false;
}

bool Session::ApplyCreateEntityNode(scenic::ResourceId id,
                                    ::gfx::EntityNodeArgs args) {
  auto node = CreateEntityNode(id, std::move(args));
  return node ? resources_.AddResource(id, std::move(node)) : false;
}

bool Session::ApplyCreateShapeNode(scenic::ResourceId id,
                                   ::gfx::ShapeNodeArgs args) {
  auto node = CreateShapeNode(id, std::move(args));
  return node ? resources_.AddResource(id, std::move(node)) : false;
}

bool Session::ApplyCreateDisplayCompositor(scenic::ResourceId id,
                                           ::gfx::DisplayCompositorArgs args) {
  auto compositor = CreateDisplayCompositor(id, std::move(args));
  return compositor ? resources_.AddResource(id, std::move(compositor)) : false;
}

bool Session::ApplyCreateImagePipeCompositor(
    scenic::ResourceId id,
    ::gfx::ImagePipeCompositorArgs args) {
  auto compositor = CreateImagePipeCompositor(id, std::move(args));
  return compositor ? resources_.AddResource(id, std::move(compositor)) : false;
}

bool Session::ApplyCreateLayerStack(scenic::ResourceId id,
                                    ::gfx::LayerStackArgs args) {
  auto layer_stack = CreateLayerStack(id, std::move(args));
  return layer_stack ? resources_.AddResource(id, std::move(layer_stack))
                     : false;
}

bool Session::ApplyCreateLayer(scenic::ResourceId id, ::gfx::LayerArgs args) {
  auto layer = CreateLayer(id, std::move(args));
  return layer ? resources_.AddResource(id, std::move(layer)) : false;
}

bool Session::ApplyCreateVariable(scenic::ResourceId id,
                                  ::gfx::VariableArgs args) {
  auto variable = CreateVariable(id, std::move(args));
  return variable ? resources_.AddResource(id, std::move(variable)) : false;
}

ResourcePtr Session::CreateMemory(scenic::ResourceId id,
                                  ::gfx::MemoryArgs args) {
  vk::Device device = engine()->vk_device();
  switch (args.memory_type) {
    case images::MemoryType::VK_DEVICE_MEMORY:
      return GpuMemory::New(this, id, device, std::move(args), error_reporter_);
    case images::MemoryType::HOST_MEMORY:
      return HostMemory::New(this, id, device, std::move(args),
                             error_reporter_);
  }
}

ResourcePtr Session::CreateImage(scenic::ResourceId id,
                                 MemoryPtr memory,
                                 ::gfx::ImageArgs args) {
  return Image::New(this, id, memory, args.info, args.memory_offset,
                    error_reporter_);
}

ResourcePtr Session::CreateBuffer(scenic::ResourceId id,
                                  MemoryPtr memory,
                                  uint32_t memory_offset,
                                  uint32_t num_bytes) {
  if (!memory->IsKindOf<GpuMemory>()) {
    // TODO(MZ-273): host memory should also be supported.
    error_reporter_->ERROR() << "scenic::gfx::Session::CreateBuffer(): "
                                "memory must be of type "
                                "ui.gfx.MemoryType.VK_DEVICE_MEMORY";
    return ResourcePtr();
  }

  auto gpu_memory = memory->As<GpuMemory>();
  if (memory_offset + num_bytes > gpu_memory->size()) {
    error_reporter_->ERROR() << "scenic::gfx::Session::CreateBuffer(): "
                                "buffer does not fit within memory (buffer "
                                "offset: "
                             << memory_offset << ", buffer size: " << num_bytes
                             << ", memory size: " << gpu_memory->size() << ")";
    return ResourcePtr();
  }

  return fxl::MakeRefCounted<Buffer>(this, id, std::move(gpu_memory), num_bytes,
                                     memory_offset);
}

ResourcePtr Session::CreateScene(scenic::ResourceId id, ::gfx::SceneArgs args) {
  return fxl::MakeRefCounted<Scene>(this, id);
}

ResourcePtr Session::CreateCamera(scenic::ResourceId id,
                                  ::gfx::CameraArgs args) {
  if (auto scene = resources_.FindResource<Scene>(args.scene_id)) {
    return fxl::MakeRefCounted<Camera>(this, id, std::move(scene));
  }
  return ResourcePtr();
}

ResourcePtr Session::CreateStereoCamera(scenic::ResourceId id,
                                        const ::gfx::StereoCameraArgs args) {
  if (auto scene = resources_.FindResource<Scene>(args.scene_id)) {
    return fxl::MakeRefCounted<StereoCamera>(this, id, std::move(scene));
  }
  return ResourcePtr();
}

ResourcePtr Session::CreateRenderer(scenic::ResourceId id,
                                    ::gfx::RendererArgs args) {
  return fxl::MakeRefCounted<Renderer>(this, id);
}

ResourcePtr Session::CreateAmbientLight(scenic::ResourceId id) {
  return fxl::MakeRefCounted<AmbientLight>(this, id);
}

ResourcePtr Session::CreateDirectionalLight(scenic::ResourceId id) {
  return fxl::MakeRefCounted<DirectionalLight>(this, id);
}

ResourcePtr Session::CreateClipNode(scenic::ResourceId id,
                                    ::gfx::ClipNodeArgs args) {
  error_reporter_->ERROR() << "scenic::gfx::Session::CreateClipNode(): "
                              "unimplemented.";
  return ResourcePtr();
}

ResourcePtr Session::CreateEntityNode(scenic::ResourceId id,
                                      ::gfx::EntityNodeArgs args) {
  return fxl::MakeRefCounted<EntityNode>(this, id);
}

ResourcePtr Session::CreateShapeNode(scenic::ResourceId id,
                                     ::gfx::ShapeNodeArgs args) {
  return fxl::MakeRefCounted<ShapeNode>(this, id);
}

ResourcePtr Session::CreateDisplayCompositor(
    scenic::ResourceId id,
    ::gfx::DisplayCompositorArgs args) {
  Display* display = engine()->display_manager()->default_display();
  if (!display) {
    error_reporter_->ERROR() << "There is no default display available.";
    return nullptr;
  }

  if (display->is_claimed()) {
    error_reporter_->ERROR() << "The default display has already been claimed "
                                "by another compositor.";
    return nullptr;
  }
  return fxl::MakeRefCounted<DisplayCompositor>(
      this, id, display, engine()->CreateDisplaySwapchain(display));
}

ResourcePtr Session::CreateImagePipeCompositor(
    scenic::ResourceId id,
    ::gfx::ImagePipeCompositorArgs args) {
  // TODO(MZ-179)
  error_reporter_->ERROR()
      << "scenic::gfx::Session::ApplyCreateImagePipeCompositor() "
         "is unimplemented (MZ-179)";
  return ResourcePtr();
}

ResourcePtr Session::CreateLayerStack(scenic::ResourceId id,
                                      ::gfx::LayerStackArgs args) {
  return fxl::MakeRefCounted<LayerStack>(this, id);
}

ResourcePtr Session::CreateVariable(scenic::ResourceId id,
                                    ::gfx::VariableArgs args) {
  fxl::RefPtr<Variable> variable;
  switch (args.type) {
    case ::gfx::ValueType::kVector1:
      variable = fxl::MakeRefCounted<FloatVariable>(this, id);
      break;
    case ::gfx::ValueType::kVector2:
      variable = fxl::MakeRefCounted<Vector2Variable>(this, id);
      break;
    case ::gfx::ValueType::kVector3:
      variable = fxl::MakeRefCounted<Vector3Variable>(this, id);
      break;
    case ::gfx::ValueType::kVector4:
      variable = fxl::MakeRefCounted<Vector4Variable>(this, id);
      break;
    case ::gfx::ValueType::kMatrix4:
      variable = fxl::MakeRefCounted<Matrix4x4Variable>(this, id);
      break;
    case ::gfx::ValueType::kColorRgb:
      // not yet supported
      variable = nullptr;
      break;
    case ::gfx::ValueType::kColorRgba:
      // not yet supported
      variable = nullptr;
      break;
    case ::gfx::ValueType::kQuaternion:
      variable = fxl::MakeRefCounted<QuaternionVariable>(this, id);
      break;
    case ::gfx::ValueType::kFactoredTransform:
      /* variable = fxl::MakeRefCounted<TransformVariable>(this, id); */
      variable = nullptr;
      break;
    case ::gfx::ValueType::kNone:
      break;
  }
  if (variable && variable->SetValue(args.initial_value)) {
    return variable;
  }
  return nullptr;
}

ResourcePtr Session::CreateLayer(scenic::ResourceId id, ::gfx::LayerArgs args) {
  return fxl::MakeRefCounted<Layer>(this, id);
}

ResourcePtr Session::CreateCircle(scenic::ResourceId id, float initial_radius) {
  return fxl::MakeRefCounted<CircleShape>(this, id, initial_radius);
}

ResourcePtr Session::CreateRectangle(scenic::ResourceId id,
                                     float width,
                                     float height) {
  return fxl::MakeRefCounted<RectangleShape>(this, id, width, height);
}

ResourcePtr Session::CreateRoundedRectangle(scenic::ResourceId id,
                                            float width,
                                            float height,
                                            float top_left_radius,
                                            float top_right_radius,
                                            float bottom_right_radius,
                                            float bottom_left_radius) {
  auto factory = engine()->escher_rounded_rect_factory();
  if (!factory) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::CreateRoundedRectangle(): "
           "no RoundedRectFactory available.";
    return ResourcePtr();
  }

  // If radii sum exceeds width or height, scale them down.
  float top_radii_sum = top_left_radius + top_right_radius;
  float top_scale = std::min(width / top_radii_sum, 1.f);

  float bottom_radii_sum = bottom_left_radius + bottom_right_radius;
  float bottom_scale = std::min(width / bottom_radii_sum, 1.f);

  float left_radii_sum = top_left_radius + bottom_left_radius;
  float left_scale = std::min(height / left_radii_sum, 1.f);

  float right_radii_sum = top_right_radius + bottom_right_radius;
  float right_scale = std::min(height / right_radii_sum, 1.f);

  top_left_radius *= std::min(top_scale, left_scale);
  top_right_radius *= std::min(top_scale, right_scale);
  bottom_left_radius *= std::min(bottom_scale, left_scale);
  bottom_right_radius *= std::min(bottom_scale, right_scale);

  escher::RoundedRectSpec rect_spec(width, height, top_left_radius,
                                    top_right_radius, bottom_right_radius,
                                    bottom_left_radius);
  escher::MeshSpec mesh_spec{escher::MeshAttribute::kPosition2D |
                             escher::MeshAttribute::kUV};

  return fxl::MakeRefCounted<RoundedRectangleShape>(
      this, id, rect_spec, factory->NewRoundedRect(rect_spec, mesh_spec));
}

ResourcePtr Session::CreateMesh(scenic::ResourceId id) {
  return fxl::MakeRefCounted<MeshShape>(this, id);
}

ResourcePtr Session::CreateMaterial(scenic::ResourceId id) {
  return fxl::MakeRefCounted<Material>(this, id);
}

void Session::TearDown() {
  if (!is_valid_) {
    // TearDown already called.
    return;
  }
  is_valid_ = false;
  resources_.Clear();
  scheduled_image_pipe_updates_ = {};

  // We assume the channel for the associated ::gfx::Session is closed because
  // SessionHandler closes it before calling this method.
  // The channel *must* be closed before we clear |scheduled_updates_|, since it
  // contains pending callbacks to ::gfx::Session::Present(); if it were not
  // closed, we would have to invoke those callbacks before destroying them.
  scheduled_updates_ = {};
  fences_to_release_on_next_update_.reset();

  if (resource_count_ != 0) {
    auto exported_count =
        engine()->resource_linker()->NumExportsForSession(this);
    FXL_CHECK(resource_count_ == 0)
        << "Session::TearDown(): Not all resources have been collected. "
           "Exported resources: "
        << exported_count
        << ", total outstanding resources: " << resource_count_;
  }
  error_reporter_ = nullptr;
}

ErrorReporter* Session::error_reporter() const {
  return error_reporter_ ? error_reporter_ : ErrorReporter::Default();
}

bool Session::AssertValueIsOfType(const ::gfx::Value& value,
                                  const ::gfx::Value::Tag* tags,
                                  size_t tag_count) {
  FXL_DCHECK(tag_count > 0);
  for (size_t i = 0; i < tag_count; ++i) {
    if (value.Which() == tags[i]) {
      return true;
    }
  }
  std::ostringstream str;
  if (tag_count == 1) {
    str << ", which is not the expected type: " << tags[0] << ".";
  } else {
    str << ", which is not one of the expected types (" << tags[0];
    for (size_t i = 1; i < tag_count; ++i) {
      str << ", " << tags[i];
    }
    str << ").";
  }
  error_reporter_->ERROR() << "scenic::gfx::Session: received value of type: "
                           << value.Which() << str.str();
  return false;
}

bool Session::ScheduleUpdate(uint64_t presentation_time,
                             std::vector<::gfx::Command> commands,
                             ::fidl::VectorPtr<zx::event> acquire_fences,
                             ::fidl::VectorPtr<zx::event> release_events,
                             ui::Session::PresentCallback callback) {
  if (is_valid()) {
    uint64_t last_scheduled_presentation_time =
        last_applied_update_presentation_time_;
    if (!scheduled_updates_.empty()) {
      last_scheduled_presentation_time =
          std::max(last_scheduled_presentation_time,
                   scheduled_updates_.back().presentation_time);
    }

    if (presentation_time < last_scheduled_presentation_time) {
      error_reporter_->ERROR()
          << "scenic::gfx::Session: Present called with out-of-order "
             "presentation time. "
          << "presentation_time=" << presentation_time
          << ", last scheduled presentation time="
          << last_scheduled_presentation_time << ".";
      return false;
    }
    auto acquire_fence_set =
        std::make_unique<escher::FenceSetListener>(std::move(acquire_fences));
    // TODO: Consider calling ScheduleUpdateForSession immediately if
    // acquire_fence_set is already ready (which is the case if there are
    // zero acquire fences).

    acquire_fence_set->WaitReadyAsync(
        [weak = weak_factory_.GetWeakPtr(), presentation_time] {
          if (weak)
            weak->engine_->session_manager()->ScheduleUpdateForSession(
                presentation_time, SessionPtr(weak.get()));
        });

    scheduled_updates_.push(Update{presentation_time, std::move(commands),
                                   std::move(acquire_fence_set),
                                   std::move(release_events), callback});
  }
  return true;
}

void Session::ScheduleImagePipeUpdate(uint64_t presentation_time,
                                      ImagePipePtr image_pipe) {
  if (is_valid()) {
    scheduled_image_pipe_updates_.push(
        {presentation_time, std::move(image_pipe)});

    engine_->session_manager()->ScheduleUpdateForSession(presentation_time,
                                                         SessionPtr(this));
  }
}

bool Session::ApplyScheduledUpdates(uint64_t presentation_time,
                                    uint64_t presentation_interval) {
  TRACE_DURATION("gfx", "Session::ApplyScheduledUpdates", "id", id_, "time",
                 presentation_time, "interval", presentation_interval);

  if (presentation_time < last_presentation_time_) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session: ApplyScheduledUpdates called with "
           "presentation_time="
        << presentation_time << ", which is less than last_presentation_time_="
        << last_presentation_time_ << ".";
    return false;
  }

  bool needs_render = false;
  while (!scheduled_updates_.empty() &&
         scheduled_updates_.front().presentation_time <= presentation_time &&
         scheduled_updates_.front().acquire_fences->ready()) {
    if (ApplyUpdate(std::move(scheduled_updates_.front().commands))) {
      needs_render = true;
      auto info = images::PresentationInfo();
      info.presentation_time = presentation_time;
      info.presentation_interval = presentation_interval;
      scheduled_updates_.front().present_callback(std::move(info));

      FXL_DCHECK(last_applied_update_presentation_time_ <=
                 scheduled_updates_.front().presentation_time);
      last_applied_update_presentation_time_ =
          scheduled_updates_.front().presentation_time;

      for (size_t i = 0; i < fences_to_release_on_next_update_->size(); ++i) {
        engine()->release_fence_signaller()->AddCPUReleaseFence(
            std::move(fences_to_release_on_next_update_->at(i)));
      }
      fences_to_release_on_next_update_ =
          std::move(scheduled_updates_.front().release_fences);

      scheduled_updates_.pop();

      // TODO: gather statistics about how close the actual
      // presentation_time was to the requested time.
    } else {
      // An error was encountered while applying the update.
      FXL_LOG(WARNING) << "mozart::Session::ApplyScheduledUpdates(): "
                          "An error was encountered while applying the update. "
                          "Initiating teardown.";

      scheduled_updates_ = {};

      BeginTearDown();

      // Tearing down a session will very probably result in changes to
      // the global scene-graph.
      return true;
    }
  }

  // TODO: Unify with other session updates.
  while (!scheduled_image_pipe_updates_.empty() &&
         scheduled_image_pipe_updates_.top().presentation_time <=
             presentation_time) {
    needs_render = scheduled_image_pipe_updates_.top().image_pipe->Update(
        presentation_time, presentation_interval);
    scheduled_image_pipe_updates_.pop();
  }

  return needs_render;
}

void Session::EnqueueEvent(::gfx::Event event) {
  if (is_valid()) {
    if (buffered_events_->empty()) {
      async::PostTask(async_get_default(), [weak = weak_factory_.GetWeakPtr()] {
        if (weak)
          weak->FlushEvents();
      });
    }
    ui::Event scenic_event;
    scenic_event.set_gfx(std::move(event));
    buffered_events_.push_back(std::move(scenic_event));
  }
}

void Session::FlushEvents() {
  if (!buffered_events_->empty() && event_reporter_) {
    event_reporter_->SendEvents(std::move(buffered_events_));
  }
}

bool Session::ApplyUpdate(std::vector<::gfx::Command> commands) {
  TRACE_DURATION("gfx", "Session::ApplyUpdate");
  if (is_valid()) {
    for (auto& command : commands) {
      if (!ApplyCommand(std::move(command))) {
        error_reporter_->ERROR()
            << "scenic::gfx::Session::ApplyCommand() failed to apply Command: "
            << command;
        return false;
      }
    }
  }
  return true;
  // TODO: acquire_fences and release_fences should be added to a list that is
  // consumed by the FrameScheduler.
}

void Session::HitTest(uint32_t node_id,
                      ::gfx::vec3 ray_origin,
                      ::gfx::vec3 ray_direction,
                      ui::Session::HitTestCallback callback) {
  if (auto node = resources_.FindResource<Node>(node_id)) {
    HitTester hit_tester;
    std::vector<Hit> hits = hit_tester.HitTest(
        node.get(), escher::ray4{escher::vec4(Unwrap(ray_origin), 1.f),
                                 escher::vec4(Unwrap(ray_direction), 0.f)});
    callback(WrapHits(hits));
  } else {
    // TODO(MZ-162): Currently the test fails if the node isn't presented yet.
    // Perhaps we should given clients more control over which state of
    // the scene graph will be consulted for hit testing purposes.
    error_reporter_->WARN()
        << "Cannot perform hit test because node " << node_id
        << " does not exist in the currently presented content.";
    callback(nullptr);
  }
}

void Session::HitTestDeviceRay(::gfx::vec3 ray_origin,
                               ::gfx::vec3 ray_direction,
                               ui::Session::HitTestCallback callback) {
  escher::ray4 ray =
      escher::ray4{{Unwrap(ray_origin), 1.f}, {Unwrap(ray_direction), 0.f}};

  // The layer stack expects the input to the hit test to be in unscaled device
  // coordinates.
  std::vector<Hit> layer_stack_hits =
      engine_->GetFirstCompositor()->layer_stack()->HitTest(ray, this);

  callback(WrapHits(layer_stack_hits));
}

void Session::BeginTearDown() {
  engine()->session_manager()->TearDownSession(id());
  FXL_DCHECK(!is_valid());
}

}  // namespace gfx
}  // namespace scenic
