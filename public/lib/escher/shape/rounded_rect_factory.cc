// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/shape/rounded_rect_factory.h"

#include "lib/escher/escher.h"
#include "lib/escher/renderer/batch_gpu_uploader.h"
#include "lib/escher/shape/mesh.h"
#include "lib/escher/shape/mesh_spec.h"
#include "lib/escher/vk/buffer_factory.h"

namespace escher {

RoundedRectFactory::RoundedRectFactory(EscherWeakPtr weak_escher)
    : ResourceRecycler(std::move(weak_escher)),
      buffer_factory_(std::make_unique<BufferFactory>(GetEscherWeakPtr())) {}

RoundedRectFactory::~RoundedRectFactory() {}

MeshPtr RoundedRectFactory::NewRoundedRect(
    const RoundedRectSpec& spec, const MeshSpec& mesh_spec,
    BatchGpuUploader* batch_gpu_uploader) {
  FXL_DCHECK(batch_gpu_uploader);

  auto index_buffer = GetIndexBuffer(spec, mesh_spec, batch_gpu_uploader);

  auto counts = GetRoundedRectMeshVertexAndIndexCounts(spec);
  const uint32_t vertex_count = counts.first;
  const uint32_t index_count = counts.second;
  const size_t primary_buffer_stride = mesh_spec.stride(0);
  const size_t secondary_buffer_stride = mesh_spec.stride(1);

  const size_t vertex_buffer_size =
      vertex_count * (primary_buffer_stride + secondary_buffer_stride);

  auto vertex_buffer =
      buffer_factory_->NewBuffer(vertex_buffer_size,
                                 vk::BufferUsageFlagBits::eVertexBuffer |
                                     vk::BufferUsageFlagBits::eTransferDst,
                                 vk::MemoryPropertyFlagBits::eDeviceLocal);

  auto writer = batch_gpu_uploader->AcquireWriter(vertex_buffer_size);
  GenerateRoundedRectVertices(spec, mesh_spec, writer->host_ptr(),
                              writer->size());
  writer->WriteBuffer(vertex_buffer, {0, 0, vertex_buffer->size()},
                      Semaphore::New(vk_device()));
  batch_gpu_uploader->PostWriter(std::move(writer));

  const BoundingBox bounding_box =
      spec.width > 0.f && spec.height > 0.f
          ? BoundingBox(-0.5f * vec3(spec.width, spec.height, 0),
                        0.5f * vec3(spec.width, spec.height, 0))
          : BoundingBox();

  switch (mesh_spec.vertex_buffer_count()) {
    case 1:
      return fxl::MakeRefCounted<Mesh>(
          static_cast<ResourceRecycler*>(this), mesh_spec, bounding_box,
          vertex_count, index_count, vertex_buffer, std::move(index_buffer));
    case 2:
      return fxl::MakeRefCounted<Mesh>(
          static_cast<ResourceRecycler*>(this), mesh_spec, bounding_box,
          vertex_count, index_count, vertex_buffer, std::move(vertex_buffer),
          std::move(index_buffer), 0, vertex_count * primary_buffer_stride, 0);
    default:
      FXL_CHECK(false) << "unsupported vertex buffer count: "
                       << mesh_spec.vertex_buffer_count();
      return nullptr;
  }
}

BufferPtr RoundedRectFactory::GetIndexBuffer(
    const RoundedRectSpec& spec, const MeshSpec& mesh_spec,
    BatchGpuUploader* batch_gpu_uploader) {
  FXL_DCHECK(batch_gpu_uploader);
  // Lazily create index buffer.  Since the rounded-rect tessellation functions
  // don't currently take |RoundedRectSpec.zoom| into account, we can always
  // return the same index buffer.
  if (!index_buffer_) {
    uint32_t index_count = GetRoundedRectMeshVertexAndIndexCounts(spec).second;
    size_t index_buffer_size = index_count * sizeof(MeshSpec::IndexType);

    index_buffer_ =
        buffer_factory_->NewBuffer(index_buffer_size,
                                   vk::BufferUsageFlagBits::eIndexBuffer |
                                       vk::BufferUsageFlagBits::eTransferDst,
                                   vk::MemoryPropertyFlagBits::eDeviceLocal);

    auto writer = batch_gpu_uploader->AcquireWriter(index_buffer_size);
    GenerateRoundedRectIndices(spec, mesh_spec, writer->host_ptr(),
                               writer->size());
    writer->WriteBuffer(index_buffer_, {0, 0, index_buffer_->size()},
                        SemaphorePtr());
    batch_gpu_uploader->PostWriter(std::move(writer));
  }
  return index_buffer_;
}

}  // namespace escher
