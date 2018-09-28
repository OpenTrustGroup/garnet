// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_SHAPE_MESH_SPEC_H_
#define LIB_ESCHER_SHAPE_MESH_SPEC_H_

// TODO(ES-132): Add Flags type so that MeshAttribute doesn't need vk::Flags.
#include <vulkan/vulkan.hpp>

#include "lib/escher/vk/vulkan_limits.h"

namespace escher {

enum class MeshAttribute {
  // vec2.  Position of the vertex, to be transformed by model-view-projection
  // (MVP) matrix.
  kPosition2D = 1,
  // vec3.  Position of the vertex, to be transformed by model-view-projection
  // (MVP) matrix.
  kPosition3D = 1 << 1,
  // vec2.  Scalable position offset.  If this is present, add (some scaled
  // version of) this to the position attribute before multiplying by the
  // MVP matrix.
  kPositionOffset = 1 << 2,
  // vec2.  UV surface parameterization, often used as texture coordinates.
  kUV = 1 << 3,
  // float. Parameterization around the perimeter of an shape, which varies from
  // 0 - 1, and allows the vertex shader to know "where it is" on the shape.
  kPerimeterPos = 1 << 4,
  // float.  Describes how much this vertex should be affected by some
  // transformation implemented by the vertex shader.
  kBlendWeight1 = 1 << 5,
  // Pseudo-attribute, used to obtain the vertex stride for the mesh.
  kStride = 1 << 6,
};

using MeshAttributes = vk::Flags<MeshAttribute, uint32_t>;

inline MeshAttributes operator|(MeshAttribute bit0, MeshAttribute bit1) {
  return MeshAttributes(bit0) | bit1;
}

// Return the per-vertex size of the specified attribute, as documented above
// (e.g. kPosition2D == sizeof(vec2)).
uint32_t GetMeshAttributeSize(MeshAttribute attr);

// Return the byte-offset of the specified attribute |attr| within a vertex that
// that contains all of the attributes specified by |attributes|.  For example,
// if |attributes| is "kPosition3D | kUV" and |attr| is "kUV" then the result
// will be 12, because the UV coordinates will immediately follow the vec3
// position, and sizeof(vec3) == 12.
//
// NOTE: this can also be used to find the stride of the vertex.  In the above
// example, if we replace |attr| with "kStride", then the result will be 20,
// because the vertex consists of a vec3 position followed by vec2 UV coords,
// and sizeof(vec3) + sizeof(vec2) == 20.
uint32_t GetMeshAttributeOffset(const MeshAttributes& attributes,
                                MeshAttribute attr);

// Describes the format of a mesh with either one or two attribute buffers,
// and the layout of attributes within those buffers.
struct MeshSpec {
  using IndexType = uint32_t;
  static constexpr vk::IndexType IndexTypeEnum = vk::IndexType::eUint32;

  // Describes the vertex attributes for each vertex buffer bound by the mesh.
  // Requirements:
  // - the same attribute cannot appear in multiple vertex buffers.
  // - there must be exatly one position attribute (either 2D or 3D), and it
  //   must appear in the first vertex buffer.
  std::array<MeshAttributes, VulkanLimits::kNumVertexBuffers> attributes;

  // Return the number of attributes in the specified vertex buffer.
  uint32_t attribute_count(uint32_t vertex_buffer_index) const;

  // Return the total number of attributes in all vertex buffers.
  uint32_t total_attribute_count() const;

  // Delegates to GetMeshAttributeOffset() after verifying that
  // |vertex_buffer_index| is sane.
  uint32_t attribute_offset(uint32_t vertex_buffer_index,
                            MeshAttribute attr) const;

  // Return true if the specified vertex buffer has the specifed attribute, and
  // false otherwise.
  bool has_attribute(uint32_t vertex_buffer_index, MeshAttribute attr) const;

  // Return true if the specified vertex buffer has the specifed attributes, and
  // false otherwise.
  bool has_attributes(uint32_t vertex_buffer_index, MeshAttributes attrs) const;

  // Return the number of vertex buffers that have at least one attribute.
  uint32_t vertex_buffer_count() const;

  uint32_t stride(uint32_t vertex_buffer_index) const {
    return attribute_offset(vertex_buffer_index, MeshAttribute::kStride);
  }

  // Return the union of all attributes, from all vertex buffers.
  MeshAttributes all_attributes() const;

  // There must be exactly one position attribute (either 2D or 3D), and it must
  // appear in the first vertex buffer.
  bool IsValid() const;

  // This is a hack that describes the "currently supported" mesh formats in
  // Escher, i.e. the ones that tessellators know how to tessellate, and that
  // renderers know how to render.  Just because a MeshSpec describes a valid
  // one- or two-buffer mesh doesn't mean that all parts of Escher will be able
  // to deal with it, e.g. ModelDisplayListBuilder can only deal with one-buffer
  // meshes.
  bool IsValidOneBufferMesh() const;

  struct HashMapHasher {
    std::size_t operator()(const MeshSpec& spec) const;
  };
};

// Inline function definitions.

inline bool operator==(const MeshSpec& spec1, const MeshSpec& spec2) {
  return spec1.attributes == spec2.attributes;
}

// Debugging.
std::ostream& operator<<(std::ostream& str, const MeshAttribute& attr);
std::ostream& operator<<(std::ostream& str, const MeshSpec& spec);

}  // namespace escher

#endif  // LIB_ESCHER_SHAPE_MESH_SPEC_H_
