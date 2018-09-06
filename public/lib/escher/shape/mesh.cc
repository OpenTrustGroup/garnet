// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/shape/mesh.h"
#include "lib/escher/resources/resource_recycler.h"
#include "lib/escher/vk/buffer.h"

namespace escher {

const ResourceTypeInfo Mesh::kTypeInfo("Mesh", ResourceType::kResource,
                                       ResourceType::kWaitableResource,
                                       ResourceType::kMesh);

Mesh::Mesh(ResourceRecycler* resource_recycler, MeshSpec spec,
           BoundingBox bounding_box, uint32_t num_vertices,
           uint32_t num_indices,
           std::array<AttributeBuffer, VulkanLimits::kNumVertexBuffers>
               attribute_buffers,
           BufferPtr index_buffer, vk::DeviceSize index_buffer_offset)
    : WaitableResource(resource_recycler),
      spec_(std::move(spec)),
      bounding_box_(bounding_box),
      num_vertices_(num_vertices),
      num_indices_(num_indices),
      attribute_buffers_(std::move(attribute_buffers)),
      vk_index_buffer_(index_buffer->vk()),
      index_buffer_(std::move(index_buffer)),
      index_buffer_offset_(index_buffer_offset) {
  FXL_DCHECK(spec.IsValid());
  FXL_DCHECK(num_indices_ * sizeof(uint32_t) + index_buffer_offset_ <=
             index_buffer_->size());
}

// Helper for public constructors.
static Mesh::AttributeBufferArray GenerateAttributeBufferArray(
    uint32_t num_vertices, const MeshSpec& spec,
    std::array<BufferPtr, VulkanLimits::kNumVertexBuffers> buffers,
    std::array<vk::DeviceSize, VulkanLimits::kNumVertexBuffers> offsets) {
  Mesh::AttributeBufferArray result;

  for (size_t i = 0; i < VulkanLimits::kNumVertexBuffers; ++i) {
    if (buffers[i]) {
      FXL_DCHECK(spec.attribute_count(i) > 0) << "Buffer has no attributes.";

      auto& ab = result[i];
      ab.buffer = std::move(buffers[i]);
      ab.vk_buffer = ab.buffer->vk();
      ab.offset = offsets[i];
      ab.stride = spec.stride(i);

      FXL_DCHECK(num_vertices * ab.stride + ab.offset <= ab.buffer->size());
    } else {
      FXL_DCHECK(spec.attribute_count(i) == 0) << "Missing attribute buffer.";
    }
  }

  return result;
}

Mesh::Mesh(ResourceRecycler* resource_recycler, MeshSpec spec,
           BoundingBox bounding_box, uint32_t num_vertices,
           uint32_t num_indices, BufferPtr primary_attribute_buffer,
           BufferPtr secondary_attribute_buffer, BufferPtr index_buffer,
           vk::DeviceSize primary_attribute_buffer_offset,
           vk::DeviceSize secondary_attribute_buffer_offset,
           vk::DeviceSize index_buffer_offset)
    : Mesh(resource_recycler, spec, bounding_box, num_vertices, num_indices,
           GenerateAttributeBufferArray(
               num_vertices, spec,
               {
                   std::move(primary_attribute_buffer),
                   std::move(secondary_attribute_buffer),
               },
               {primary_attribute_buffer_offset,
                secondary_attribute_buffer_offset}),
           std::move(index_buffer), index_buffer_offset) {}

Mesh::Mesh(ResourceRecycler* resource_recycler, MeshSpec spec,
           BoundingBox bounding_box, uint32_t num_vertices,
           uint32_t num_indices, BufferPtr primary_attribute_buffer,
           BufferPtr index_buffer,
           vk::DeviceSize primary_attribute_buffer_offset,
           vk::DeviceSize index_buffer_offset)
    : Mesh(resource_recycler, spec, bounding_box, num_vertices, num_indices,
           GenerateAttributeBufferArray(num_vertices, spec,
                                        {std::move(primary_attribute_buffer)},
                                        {primary_attribute_buffer_offset}),
           std::move(index_buffer), index_buffer_offset) {}

Mesh::~Mesh() {}

}  // namespace escher
