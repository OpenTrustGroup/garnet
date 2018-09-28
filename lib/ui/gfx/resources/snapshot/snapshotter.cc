// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/snapshot/snapshotter.h"

#include <lib/zx/vmo.h>
#include "garnet/lib/ui/gfx/resources/buffer.h"
#include "garnet/lib/ui/gfx/resources/camera.h"
#include "garnet/lib/ui/gfx/resources/compositor/display_compositor.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer_stack.h"
#include "garnet/lib/ui/gfx/resources/gpu_memory.h"
#include "garnet/lib/ui/gfx/resources/host_memory.h"
#include "garnet/lib/ui/gfx/resources/image.h"
#include "garnet/lib/ui/gfx/resources/image_pipe.h"
#include "garnet/lib/ui/gfx/resources/import.h"
#include "garnet/lib/ui/gfx/resources/lights/ambient_light.h"
#include "garnet/lib/ui/gfx/resources/lights/directional_light.h"
#include "garnet/lib/ui/gfx/resources/material.h"
#include "garnet/lib/ui/gfx/resources/nodes/entity_node.h"
#include "garnet/lib/ui/gfx/resources/nodes/opacity_node.h"
#include "garnet/lib/ui/gfx/resources/nodes/scene.h"
#include "garnet/lib/ui/gfx/resources/nodes/shape_node.h"
#include "garnet/lib/ui/gfx/resources/renderers/renderer.h"
#include "garnet/lib/ui/gfx/resources/resource_visitor.h"
#include "garnet/lib/ui/gfx/resources/shapes/circle_shape.h"
#include "garnet/lib/ui/gfx/resources/shapes/mesh_shape.h"
#include "garnet/lib/ui/gfx/resources/shapes/rectangle_shape.h"
#include "garnet/lib/ui/gfx/resources/shapes/rounded_rectangle_shape.h"
#include "garnet/lib/ui/gfx/resources/snapshot/version.h"
#include "garnet/lib/ui/gfx/resources/view.h"
#include "garnet/lib/ui/gfx/resources/view_holder.h"
#include "lib/escher/vk/image.h"
#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fsl/vmo/vector.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

namespace scenic_impl {
namespace gfx {

// Helper function to create a |SizedVmo| from bytes of certain size.
bool VmoFromBytes(const uint8_t* bytes, size_t num_bytes, uint32_t type,
                  uint32_t version, fsl::SizedVmo* sized_vmo_ptr) {
  FXL_CHECK(sized_vmo_ptr);

  zx::vmo vmo;
  size_t total_size = num_bytes + sizeof(type) + sizeof(version);
  zx_status_t status = zx::vmo::create(total_size, 0u, &vmo);
  if (status < 0) {
    FXL_LOG(WARNING) << "zx::vmo::create failed: " << status;
    return false;
  }

  if (num_bytes > 0) {
    status = vmo.write(&type, 0, sizeof(uint32_t));
    if (status < 0) {
      FXL_LOG(WARNING) << "zx::vmo::write snapshot type failed: " << status;
      return false;
    }
    status = vmo.write(&version, sizeof(uint32_t), sizeof(uint32_t));
    if (status < 0) {
      FXL_LOG(WARNING) << "zx::vmo::write snapshot version failed: " << status;
      return false;
    }
    status = vmo.write(bytes, 2 * sizeof(uint32_t), num_bytes);
    if (status < 0) {
      FXL_LOG(WARNING) << "zx::vmo::write bytes failed: " << status;
      return false;
    }
  }

  *sized_vmo_ptr = fsl::SizedVmo(std::move(vmo), total_size);

  return true;
}

Snapshotter::Snapshotter(escher::BatchGpuUploaderPtr gpu_uploader)
    : gpu_uploader_(gpu_uploader) {}

void Snapshotter::TakeSnapshot(Resource* resource,
                               TakeSnapshotCallback callback) {
  FXL_DCHECK(resource) << "Cannot snapshot null resource.";
  // Visit the scene graph starting with |resource| and collecting images
  // and buffers to read from the GPU.
  resource->Accept(this);

  // Submit all images/buffers to be read from GPU.
  gpu_uploader_->Submit(
      escher::SemaphorePtr(),
      fxl::MakeCopyable([node_serializer = current_node_serializer_,
                         callback = std::move(callback)]() {
        auto builder = std::make_shared<flatbuffers::FlatBufferBuilder>();
        builder->Finish(node_serializer->serialize(*builder));

        fsl::SizedVmo sized_vmo;
        if (!VmoFromBytes(builder->GetBufferPointer(), builder->GetSize(),
                          SnapshotData::SnapshotType::kFlatBuffer,
                          SnapshotData::SnapshotVersion::v1_0, &sized_vmo)) {
          return callback(fuchsia::mem::Buffer{});
        } else {
          return callback(std::move(sized_vmo).ToTransport());
        }
      }));
}

void Snapshotter::Visit(EntityNode* r) { VisitNode(r); }
void Snapshotter::Visit(OpacityNode* r) { VisitNode(r); }
void Snapshotter::Visit(ShapeNode* r) {
  if (r->shape() && r->material()) {
    VisitNode(r);
    r->shape()->Accept(this);
    r->material()->Accept(this);
  }
}

void Snapshotter::Visit(Scene* r) { VisitNode(r); }

void Snapshotter::Visit(CircleShape* r) {
  FXL_DCHECK(current_node_serializer_);
  auto shape = std::make_shared<CircleSerializer>();
  shape->radius = r->radius();

  current_node_serializer_->shape = shape;
}

void Snapshotter::Visit(RectangleShape* r) {
  FXL_DCHECK(current_node_serializer_);

  auto shape = std::make_shared<RectangleSerializer>();
  shape->width = r->width();
  shape->height = r->height();

  current_node_serializer_->shape = shape;
}

void Snapshotter::Visit(RoundedRectangleShape* r) {
  FXL_DCHECK(current_node_serializer_);

  auto shape = std::make_shared<RoundedRectangleSerializer>();
  shape->width = r->width();
  shape->height = r->height();

  shape->top_left_radius = r->top_left_radius();
  shape->top_right_radius = r->top_right_radius();
  shape->bottom_right_radius = r->bottom_right_radius();
  shape->bottom_left_radius = r->bottom_left_radius();

  current_node_serializer_->shape = shape;

  VisitMesh(r->escher_mesh());
}

void Snapshotter::Visit(MeshShape* r) {
  FXL_DCHECK(current_node_serializer_);

  current_node_serializer_->shape = std::make_shared<MeshSerializer>();

  VisitMesh(r->escher_mesh());
}

void Snapshotter::Visit(Material* r) {
  if (r->texture_image()) {
    r->texture_image()->Accept(this);
  } else {
    auto color = std::make_shared<ColorSerializer>();
    color->red = r->red();
    color->green = r->green();
    color->blue = r->blue();
    color->alpha = r->alpha();

    current_node_serializer_->material = color;
  }

  VisitResource(r);
}

void Snapshotter::Visit(GpuMemory* r) { VisitResource(r); }
void Snapshotter::Visit(HostMemory* r) { VisitResource(r); }

void Snapshotter::Visit(Image* r) {
  if (r->GetEscherImage()) {
    VisitImage(r->GetEscherImage());
  }
  VisitResource(r);
}

void Snapshotter::Snapshotter::Visit(ImagePipe* r) {
  if (r->GetEscherImage()) {
    VisitImage(r->GetEscherImage());
  }
  VisitResource(r);
}
void Snapshotter::Visit(Buffer* r) { VisitResource(r); }
void Snapshotter::Visit(View* r) { VisitResource(r); }
void Snapshotter::Visit(ViewHolder* r) { VisitResource(r); }
void Snapshotter::Visit(Compositor* r) { r->layer_stack()->Accept(this); }

void Snapshotter::Visit(DisplayCompositor* r) {
  r->layer_stack()->Accept(this);
}
void Snapshotter::Visit(LayerStack* r) {
  for (auto& layer : r->layers()) {
    layer->Accept(this);
  }
}
void Snapshotter::Visit(Layer* r) { r->renderer()->Accept(this); }
void Snapshotter::Visit(Camera* r) { r->scene()->Accept(this); }
void Snapshotter::Visit(Renderer* r) { r->camera()->Accept(this); }
void Snapshotter::Visit(Light* r) { VisitResource(r); }
void Snapshotter::Visit(AmbientLight* r) { VisitResource(r); }
void Snapshotter::Visit(DirectionalLight* r) { VisitResource(r); }
void Snapshotter::Visit(Import* r) { r->delegate()->Accept(this); }

void Snapshotter::VisitNode(Node* r) {
  auto parent_serializer = current_node_serializer_;
  auto node_serializer = std::make_shared<NodeSerializer>();
  if (parent_serializer) {
    parent_serializer->children.push_back(node_serializer);
  }

  // Name.
  node_serializer->name = r->label();

  // Transform.
  if (!r->transform().IsIdentity()) {
    auto transform = std::make_shared<TransformSerializer>();
    node_serializer->transform = transform;

    auto& translation = r->translation();
    transform->translation =
        snapshot::Vec3(translation.x, translation.y, translation.z);

    auto& scale = r->scale();
    transform->scale = snapshot::Vec3(scale.x, scale.y, scale.z);

    auto& rotation = r->rotation();
    transform->rotation =
        snapshot::Quat(rotation.x, rotation.y, rotation.z, rotation.w);

    auto& anchor = r->anchor();
    transform->anchor = snapshot::Vec3(anchor.x, anchor.y, anchor.z);
  }

  // Children.
  for (auto& part : r->parts()) {
    // Set current node to this node during children traversal.
    current_node_serializer_ = node_serializer;
    part->Accept(this);
  }

  for (auto& child : r->children()) {
    // Set current node to this node during children traversal.
    current_node_serializer_ = node_serializer;
    child->Accept(this);
  }

  // Set current node to this node during children traversal.
  current_node_serializer_ = node_serializer;
  VisitResource(r);

  current_node_serializer_ = node_serializer;
}

void Snapshotter::VisitResource(Resource* r) {
  auto node_serializer = current_node_serializer_;
  for (auto& import : r->imports()) {
    current_node_serializer_ = node_serializer;
    import->Accept(this);
  }
  current_node_serializer_ = node_serializer;
}

void Snapshotter::VisitImage(escher::ImagePtr image) {
  auto format = (int32_t)image->format();
  auto width = image->width();
  auto height = image->height();

  ReadImage(image,
            [format, width, height, node_serializer = current_node_serializer_](
                escher::BufferPtr buffer) {
              auto image = std::make_shared<ImageSerializer>();
              image->buffer = buffer;
              image->format = format;
              image->width = width;
              image->height = height;

              node_serializer->material = image;
            });
}

void Snapshotter::VisitMesh(escher::MeshPtr mesh) {
  FXL_DCHECK(current_node_serializer_);

  auto geometry = std::make_shared<GeometrySerializer>();
  geometry->bbox_min =
      snapshot::Vec3(mesh->bounding_box().min().x, mesh->bounding_box().min().y,
                     mesh->bounding_box().min().z);
  geometry->bbox_max =
      snapshot::Vec3(mesh->bounding_box().max().x, mesh->bounding_box().max().y,
                     mesh->bounding_box().max().z);
  current_node_serializer_->mesh = geometry;

  for (int i = -1; i < (int)mesh->attribute_buffers().size(); i++) {
    // -1 implies index buffer, >=0 is attribute buffer.
    bool is_index_buffer = i == -1;
    auto src_buffer = is_index_buffer ? mesh->index_buffer()
                                      : mesh->attribute_buffer(i).buffer;
    // Attribute buffer other than primarily attribute buffers can be null.
    if (!src_buffer) {
      continue;
    }

    ReadBuffer(src_buffer, [geometry, is_index_buffer,
                            mesh](escher::BufferPtr buffer) {
      if (is_index_buffer) {
        auto indices = std::make_shared<IndexBufferSerializer>();
        indices->buffer = buffer;
        indices->index_count = mesh->num_indices();

        geometry->indices = indices;
      } else {
        auto attribute = std::make_shared<AttributeBufferSerializer>();
        attribute->buffer = buffer;
        attribute->vertex_count = mesh->num_vertices();
        attribute->stride = mesh->spec().stride(0);

        geometry->attributes.push_back(attribute);
      }
    });
  }
}

void Snapshotter::ReadImage(
    escher::ImagePtr image,
    std::function<void(escher::BufferPtr buffer)> callback) {
  vk::BufferImageCopy region;
  region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageExtent.width = image->width();
  region.imageExtent.height = image->height();
  region.imageExtent.depth = 1;
  region.bufferOffset = image->memory_offset();

  auto reader = gpu_uploader_->AcquireReader(image->memory()->size());
  reader->ReadImage(image, region, escher::SemaphorePtr());
  gpu_uploader_->PostReader(std::move(reader), std::move(callback));
}

void Snapshotter::ReadBuffer(
    escher::BufferPtr buffer,
    std::function<void(escher::BufferPtr buffer)> callback) {
  auto reader = gpu_uploader_->AcquireReader(buffer->size());
  reader->ReadBuffer(buffer, {0, 0, buffer->size()}, escher::SemaphorePtr());
  gpu_uploader_->PostReader(std::move(reader), std::move(callback));
}

}  // namespace gfx
}  // namespace scenic_impl
