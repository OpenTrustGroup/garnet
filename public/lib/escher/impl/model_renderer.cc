// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/model_renderer.h"

#include <glm/gtx/transform.hpp>
#include "lib/escher/geometry/tessellation.h"
#include "lib/escher/impl/command_buffer.h"
#include "lib/escher/impl/image_cache.h"
#include "lib/escher/impl/mesh_manager.h"
#include "lib/escher/impl/model_data.h"
#include "lib/escher/impl/model_display_list.h"
#include "lib/escher/impl/model_display_list_builder.h"
#include "lib/escher/impl/model_pipeline.h"
#include "lib/escher/impl/model_render_pass.h"
#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/scene/model.h"
#include "lib/escher/scene/shape.h"
#include "lib/escher/scene/stage.h"
#include "lib/escher/util/hash_map.h"
#include "lib/escher/util/image_utils.h"
#include "lib/escher/util/trace_macros.h"
#include "lib/escher/vk/image.h"

namespace escher {
namespace impl {

ModelRendererPtr ModelRenderer::New(Escher* escher, ModelDataPtr model_data) {
  return fxl::AdoptRef(new ModelRenderer(escher, std::move(model_data)));
}

ModelRenderer::ModelRenderer(Escher* escher, ModelDataPtr model_data)
    : escher_(escher),
      device_(escher->vk_device()),
      resource_recycler_(escher->resource_recycler()),
      model_data_(std::move(model_data)) {
  rectangle_ = CreateRectangle();
  circle_ = CreateCircle();
  white_texture_ = CreateWhiteTexture(escher);
}

ModelRenderer::~ModelRenderer() {}

ModelDisplayListPtr ModelRenderer::CreateDisplayList(
    const Stage& stage,
    const Model& model,
    const Camera& camera,
    const ModelRenderPassPtr& render_pass,
    ModelDisplayListFlags flags,
    float scale,
    const TexturePtr& shadow_texture,
    const mat4& shadow_matrix,
    vec3 ambient_light_color,
    vec3 direct_light_color,
    CommandBuffer* command_buffer) {
  TRACE_DURATION("gfx", "escher::ModelRenderer::CreateDisplayList",
                 "object_count", model.objects().size());

  const std::vector<Object>& objects = model.objects();

  // TODO(ES-29): not low-hanging fruit, but maybe someday...
  FXL_DCHECK(
      !(flags & ModelDisplayListFlag::kShareDescriptorSetsBetweenObjects))
      << "unimplemented (ES-29).";

  // Used to accumulate indices of objects in render-order.
  std::vector<uint32_t> opaque_objects;
  opaque_objects.reserve(objects.size());
  // TODO: Translucency.  When rendering translucent objects, we will need a
  // separate bin for all translucent objects, and need to sort the objects in
  // that bin from back-to-front.  Conceivably, we could relax this ordering
  // requirement in cases where we can prove that the translucent objects don't
  // overlap.

  // TODO: We should sort according to more different metrics, and look for
  // performance differences between them.  At the same time, we should
  // experiment with strategies for updating/binding descriptor-sets.
  const bool sort_by_pipeline(flags & ModelDisplayListFlag::kSortByPipeline);
  if (!sort_by_pipeline) {
    // Simply render objects in the order that they appear in the model.
    for (uint32_t i = 0; i < objects.size(); ++i) {
      opaque_objects.push_back(i);
    }
  } else {
    TRACE_DURATION("gfx", "escher::ModelRenderer::CreateDisplayList[sort]");

    // Sort all objects into bins.  Then, iterate over each bin in arbitrary
    // order, without additional sorting within the bin.
    HashMap<ModelPipelineSpec, std::vector<size_t>> pipeline_bins;
    for (size_t i = 0; i < objects.size(); ++i) {
      auto& obj = objects[i];
      if (obj.shape().type() == Shape::Type::kNone) {
        // The Object is a clip-group; immediately add this to list of opaque
        // objects without binning.
        opaque_objects.push_back(i);
      } else {
        ModelPipelineSpec spec;
        spec.mesh_spec = GetMeshForShape(obj.shape())->spec();
        spec.shape_modifiers = obj.shape().modifiers();
        pipeline_bins[spec].push_back(i);
      }
    }

    for (auto& pair : pipeline_bins) {
      for (uint32_t object_index : pair.second) {
        opaque_objects.push_back(object_index);
      }
    }
  }
  FXL_DCHECK(opaque_objects.size() == objects.size());

  TRACE_DURATION("gfx", "escher::ModelRenderer::CreateDisplayList[build]");

  ModelDisplayListBuilder builder(device_, stage, model, camera, scale,
                                  white_texture_, shadow_texture, shadow_matrix,
                                  ambient_light_color, direct_light_color,
                                  model_data_.get(), this, render_pass, flags);
  for (uint32_t object_index : opaque_objects) {
    builder.AddObject(objects[object_index]);
  }
  return builder.Build(command_buffer);
}

// TODO: stage shouldn't be necessary.
void ModelRenderer::Draw(const Stage& stage,
                         const ModelDisplayListPtr& display_list,
                         CommandBuffer* command_buffer) {
  TRACE_DURATION("gfx", "escher::ModelRenderer::Draw");

  vk::CommandBuffer vk_command_buffer = command_buffer->get();

  for (const TexturePtr& texture : display_list->textures()) {
    // TODO: it would be nice if Resource::TakeWaitSemaphore() were virtual
    // so that we could say texture->TakeWaitSemaphore(), instead of needing
    // to know that the image is really the thing that we might need to wait
    // for.  Another approach would be for the Texture constructor to say
    // SetWaitSemaphore(image->TakeWaitSemaphore()), but this isn't a
    // bulletproof solution... what if someone else made a Texture with the
    // same image, and used that one first.  Of course, in general we want
    // lighter-weight synchronization such as events or barriers... need to
    // revisit this whole topic.
    command_buffer->AddWaitSemaphore(
        texture->image()->TakeWaitSemaphore(),
        vk::PipelineStageFlagBits::eFragmentShader);
  }

  vk::Viewport viewport;
  viewport.width = stage.viewing_volume().width();
  viewport.height = stage.viewing_volume().height();
  // We normalize all depths to the range [0,1].  If we didn't, then Vulkan
  // would clip them anyway.  NOTE: this is only true because we are using an
  // orthonormal projection; otherwise the depth computed by the vertex shader
  // could be outside [0,1] as long as the perspective division brought it back.
  // In this case, it might make sense to use different values for viewport
  // min/max depth.
  viewport.minDepth = 0.f;
  viewport.maxDepth = 1.f;
  vk_command_buffer.setViewport(0, 1, &viewport);

  // Retain all display-list resources until the frame is finished rendering.
  command_buffer->KeepAlive(display_list);

  vk::Pipeline current_pipeline;
  vk::PipelineLayout current_pipeline_layout;
  uint32_t current_stencil_reference = 0;
  vk_command_buffer.setStencilReference(vk::StencilFaceFlagBits::eFront, 0);
  for (const ModelDisplayList::Item& item : display_list->items()) {
    // Bind new pipeline and PerModel descriptor set, if necessary.
    if (current_pipeline != item.pipeline->pipeline()) {
      current_pipeline = item.pipeline->pipeline();
      vk_command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                     current_pipeline);

      // According to my reading of the Vulkan spec, the "valid usage"
      // requirements for vkCmdSetStencilReference() imply that it must be
      // called after binding a new pipeline:
      //   "The currently bound graphics pipeline MUST have been created with
      //    the VK_DYNAMIC_STATE_STENCIL_REFERENCE dynamic state enabled".
      // ... this implies that it will not simply be ignored if the pipeline
      // doesn't have dynamic state (i.e. it can have bad effects, which we
      // verified by experiment), which implies that the reference state is
      // stored into memory associated with the pipeline, which implies that
      // we must set it when binding a new pipeline.
      if (item.pipeline->HasDynamicStencilState()) {
        current_stencil_reference = item.stencil_reference;
        vk_command_buffer.setStencilReference(vk::StencilFaceFlagBits::eFront,
                                              current_stencil_reference);
      }

      // Whenever the pipeline changes, it is possible that the pipeline layout
      // must also change.
      if (current_pipeline_layout != item.pipeline->pipeline_layout()) {
        current_pipeline_layout = item.pipeline->pipeline_layout();
        vk::DescriptorSet ds = display_list->stage_data();
        vk_command_buffer.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics, current_pipeline_layout,
            ModelData::PerModel::kDescriptorSetIndex, 1, &ds, 0, nullptr);
      }
    }

    if (item.pipeline->HasDynamicStencilState() &&
        current_stencil_reference != item.stencil_reference) {
      current_stencil_reference = item.stencil_reference;
      vk_command_buffer.setStencilReference(vk::StencilFaceFlagBits::eFront,
                                            current_stencil_reference);
    }

    vk::DescriptorSet ds = item.descriptor_set;
    vk_command_buffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, current_pipeline_layout,
        ModelData::PerObject::kDescriptorSetIndex, 1, &ds, 0, nullptr);

    command_buffer->DrawMesh(item.mesh);
  }
}

const MeshPtr& ModelRenderer::GetMeshForShape(const Shape& shape) const {
  switch (shape.type()) {
    case Shape::Type::kRect:
      return rectangle_;
    case Shape::Type::kCircle:
      return circle_;
    case Shape::Type::kMesh:
      return shape.mesh();
    case Shape::Type::kNone: {
      FXL_DCHECK(false);
      static const MeshPtr kNone;
      return kNone;
    }
  }
}

MeshPtr ModelRenderer::CreateRectangle() {
  return NewSimpleRectangleMesh(escher_->mesh_manager());
}

MeshPtr ModelRenderer::CreateCircle() {
  MeshSpec spec{MeshAttribute::kPosition2D | MeshAttribute::kUV};
  return NewCircleMesh(escher_->mesh_manager(), spec, 4, vec2(0, 0), 1);
}

TexturePtr ModelRenderer::CreateWhiteTexture(Escher* escher) {
  uint8_t channels[4];
  channels[0] = channels[1] = channels[2] = channels[3] = 255;

  auto image = image_utils::NewRgbaImage(
      escher->image_cache(), escher->gpu_uploader(), 1, 1, channels);
  return fxl::MakeRefCounted<Texture>(escher->resource_recycler(),
                                      std::move(image), vk::Filter::eNearest);
}

}  // namespace impl
}  // namespace escher
