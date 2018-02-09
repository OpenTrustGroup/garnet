// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/util/print_op.h"

namespace scene_manager {

using scenic::Op;
using scenic::OpPtr;
using scenic::RendererParam;
using scenic::Resource;
using scenic::ShadowTechnique;
using scenic::Value;

std::ostream& operator<<(std::ostream& stream, const scenic::OpPtr& op) {
  switch (op->which()) {
    case Op::Tag::CREATE_RESOURCE:
      return stream << op->get_create_resource();
    case Op::Tag::EXPORT_RESOURCE:
      return stream << "EXPORT_RESOURCE";
    case Op::Tag::IMPORT_RESOURCE:
      return stream << "IMPORT_RESOURCE";
    case Op::Tag::RELEASE_RESOURCE:
      return stream << "RELEASE_RESOURCE";
    case Op::Tag::SET_TRANSLATION:
      return stream << "SET_TRANSLATION";
    case Op::Tag::SET_SCALE:
      return stream << "SET_SCALE";
    case Op::Tag::SET_ROTATION:
      return stream << "SET_ROTATION";
    case Op::Tag::SET_ANCHOR:
      return stream << "SET_ANCHOR";
    case Op::Tag::SET_SIZE:
      return stream << "SET_SIZE";
    case Op::Tag::SET_TAG:
      return stream << "SET_TAG";
    case Op::Tag::ADD_CHILD:
      return stream << "ADD_CHILD";
    case Op::Tag::ADD_PART:
      return stream << "ADD_PART";
    case Op::Tag::DETACH:
      return stream << "DETACH";
    case Op::Tag::DETACH_CHILDREN:
      return stream << "DETACH_CHILDREN";
    case Op::Tag::SET_SHAPE:
      return stream << "SET_SHAPE";
    case Op::Tag::SET_MATERIAL:
      return stream << "SET_MATERIAL";
    case Op::Tag::SET_CLIP:
      return stream << "SET_CLIP";
    case Op::Tag::SET_HIT_TEST_BEHAVIOR:
      return stream << "SET_HIT_TEST_BEHAVIOR";
    case Op::Tag::SET_CAMERA:
      return stream << "SET_CAMERA";
    case Op::Tag::SET_CAMERA_PROJECTION:
      return stream << "SET_CAMERA_PROJECTION";
    case scenic::Op::Tag::SET_CAMERA_POSE_BUFFER:
      return stream << "SET_CAMERA_POSE_BUFFER";
    case Op::Tag::SET_LIGHT_COLOR:
      return stream << "SET_LIGHT_COLOR";
    case Op::Tag::SET_LIGHT_DIRECTION:
      return stream << "SET_LIGHT_DIRECTION";
    case Op::Tag::ADD_LIGHT:
      return stream << "ADD_LIGHT";
    case Op::Tag::DETACH_LIGHT:
      return stream << "DETACH_LIGHT";
    case Op::Tag::DETACH_LIGHTS:
      return stream << "DETACH_LIGHTS";
    case Op::Tag::SET_TEXTURE:
      return stream << "SET_TEXTURE";
    case Op::Tag::SET_COLOR:
      return stream << "SET_COLOR";
    case Op::Tag::BIND_MESH_BUFFERS:
      return stream << "BIND_MESH_BUFFERS";
    case Op::Tag::ADD_LAYER:
      return stream << "ADD_LAYER";
    case Op::Tag::SET_LAYER_STACK:
      return stream << "SET_LAYER_STACK";
    case Op::Tag::SET_RENDERER:
      return stream << "SET_RENDERER";
    case Op::Tag::SET_RENDERER_PARAM:
      return stream << op->get_set_renderer_param();
    case Op::Tag::SET_EVENT_MASK:
      return stream << "SET_EVENT_MASK";
    case Op::Tag::SET_LABEL:
      return stream << "SET_LABEL";
    case Op::Tag::SET_DISABLE_CLIPPING:
      return stream << "SET_DISABLE_CLIPPING";
    case Op::Tag::__UNKNOWN__:
      return stream << "__UNKNOWN__";
  }
}

std::ostream& operator<<(std::ostream& stream,
                         const scenic::CreateResourceOpPtr& op) {
  stream << "CreateResourceOp(id:" << op->id << " ";
  switch (op->resource->which()) {
    case Resource::Tag::MEMORY:
      stream << "Memory";
      break;
    case Resource::Tag::IMAGE:
      stream << "Image";
      break;
    case Resource::Tag::IMAGE_PIPE:
      stream << "ImagePipe";
      break;
    case Resource::Tag::BUFFER:
      stream << "Buffer";
      break;
    case Resource::Tag::SCENE:
      stream << "Scene";
      break;
    case Resource::Tag::CAMERA:
      stream << "Camera";
      break;
    case Resource::Tag::RENDERER:
      stream << "Renderer";
      break;
    case Resource::Tag::AMBIENT_LIGHT:
      stream << "AmbientLight";
      break;
    case Resource::Tag::DIRECTIONAL_LIGHT:
      stream << "DirectionalLight";
      break;
    case Resource::Tag::RECTANGLE:
      stream << "Rectangle";
      break;
    case Resource::Tag::ROUNDED_RECTANGLE:
      stream << "RoundedRectangle";
      break;
    case Resource::Tag::CIRCLE:
      stream << "Circle";
      break;
    case Resource::Tag::MESH:
      stream << "Mesh";
      break;
    case Resource::Tag::MATERIAL:
      stream << "Material";
      break;
    case Resource::Tag::CLIP_NODE:
      stream << "ClipNode";
      break;
    case Resource::Tag::ENTITY_NODE:
      stream << "EntityNode";
      break;
    case Resource::Tag::SHAPE_NODE:
      stream << "ShapeNode";
      break;
    case Resource::Tag::DISPLAY_COMPOSITOR:
      stream << "DisplayCompositor";
      break;
    case Resource::Tag::IMAGE_PIPE_COMPOSITOR:
      stream << "ImagePipeCompositor";
      break;
    case Resource::Tag::LAYER_STACK:
      stream << "LayerStack";
      break;
    case Resource::Tag::LAYER:
      stream << "Layer";
      break;
    case Resource::Tag::VARIABLE:
      stream << "Variable";
      break;
    case Resource::Tag::__UNKNOWN__:
      stream << "__UNKNOWN__";
      break;
  }
  return stream << ")";
}

std::ostream& operator<<(std::ostream& stream,
                         const scenic::SetRendererParamOpPtr& op) {
  stream << "SetRendererParamOp(id=" << op->renderer_id << " ";
  switch (op->param->which()) {
    case RendererParam::Tag::SHADOW_TECHNIQUE:
      stream << "shadow_technique=";
      switch (op->param->get_shadow_technique()) {
        case ShadowTechnique::UNSHADOWED:
          stream << "UNSHADOWED";
          break;
        case ShadowTechnique::SCREEN_SPACE:
          stream << "SCREEN_SPACE";
          break;
        case ShadowTechnique::SHADOW_MAP:
          stream << "SHADOW_MAP";
          break;
        case ShadowTechnique::MOMENT_SHADOW_MAP:
          stream << "MOMENT_SHADOW_MAP";
          break;
      }
      break;
    case RendererParam::Tag::__UNKNOWN__:
      stream << "__UNKNOWN__";
      break;
  }
  return stream << ")";
}

std::ostream& operator<<(std::ostream& stream,
                         const scenic::SetTextureOpPtr& op) {
  stream << "SetTextureOp(id:" << op->material_id
         << " texture: " << op->texture_id;
  return stream << ")";
}

std::ostream& operator<<(std::ostream& stream,
                         const scenic::SetColorOpPtr& op) {
  stream << "SetColorOp(id:" << op->material_id;
  return stream << ")";
}

std::ostream& operator<<(std::ostream& stream, const scenic::Value::Tag& tag) {
  switch (tag) {
    case Value::Tag::VECTOR1:
      return stream << "vec1";
    case Value::Tag::VECTOR2:
      return stream << "vec2";
    case Value::Tag::VECTOR3:
      return stream << "vec3";
    case Value::Tag::VECTOR4:
      return stream << "vec4";
    case Value::Tag::MATRIX4X4:
      return stream << "mat4";
    case Value::Tag::COLOR_RGB:
      return stream << "rgb";
    case Value::Tag::COLOR_RGBA:
      return stream << "rgba";
    case Value::Tag::DEGREES:
      return stream << "degrees";
    case Value::Tag::QUATERNION:
      return stream << "quat";
    case Value::Tag::TRANSFORM:
      return stream << "transform";
    case Value::Tag::VARIABLE_ID:
      return stream << "variable";
    case Value::Tag::__UNKNOWN__:
      return stream << "__UNKNOWN__";
  }
}

}  // namespace scene_manager
