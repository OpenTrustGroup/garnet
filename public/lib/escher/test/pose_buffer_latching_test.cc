// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/escher/test/gtest_vulkan.h"
#include "garnet/public/lib/escher/escher.h"
#include "garnet/public/lib/escher/vk/buffer.h"
#include "garnet/public/lib/escher/renderer/frame.h"
#include "garnet/public/lib/escher/resources/resource_recycler.h"
#include "garnet/public/lib/escher/scene/camera.h"
#include "gtest/gtest.h"
#include "lib/escher/hmd/pose_buffer.h"
#include "garnet/public/lib/escher/hmd/pose_buffer_latching_shader.h"

#include <zx/time.h>
#include <glm/gtc/type_ptr.hpp>

namespace escher {
namespace test {

static constexpr bool EPSILON_ERROR_DETAIL = false;

// Returns true iff |f0| and |f1| are the same within optional |epsilon|.
bool CompareFloat(float f0, float f1, float epsilon = 0.0) {
  bool compare = glm::abs(f0 - f1) <= epsilon;
  if (!compare && EPSILON_ERROR_DETAIL)
    FXL_LOG(WARNING) << "floats " << f0 << " and " << f1 << " differ by "
                     << glm::abs(f0 - f1)
                     << " which is greater than provided epsilon " << epsilon;
  return compare;
}

// Returns true iff |a| and |b| are the same within optional |epsilon|.
bool ComparePose(escher::hmd::Pose* p0,
                 escher::hmd::Pose* p1,
                 float epsilon = 0.0) {
  bool compare = true;

  EXPECT_TRUE(CompareFloat(p0->a, p1->a, epsilon));
  compare = compare && CompareFloat(p0->a, p1->a, epsilon);

  EXPECT_TRUE(CompareFloat(p0->b, p1->b, epsilon));
  compare = compare && CompareFloat(p0->b, p1->b, epsilon);

  EXPECT_TRUE(CompareFloat(p0->c, p1->c, epsilon));
  compare = compare && CompareFloat(p0->c, p1->c, epsilon);

  EXPECT_TRUE(CompareFloat(p0->d, p1->d, epsilon));
  compare = compare && CompareFloat(p0->d, p1->d, epsilon);

  EXPECT_TRUE(CompareFloat(p0->x, p1->x, epsilon));
  compare = compare && CompareFloat(p0->x, p1->x, epsilon);

  EXPECT_TRUE(CompareFloat(p0->y, p1->y, epsilon));
  compare = compare && CompareFloat(p0->y, p1->y, epsilon);

  EXPECT_TRUE(CompareFloat(p0->z, p1->z, epsilon));
  compare = compare && CompareFloat(p0->z, p1->z, epsilon);

  return true;
}

void PrintMatrix(glm::mat4 m) {
  for (uint32_t i = 0; i < 4; i++) {
    for (uint32_t j = 0; j < 4; j++) {
      printf("%6.3f, ", m[j][i]);
    }
    printf("\n");
  }
  printf("\n");
}

bool CompareMatrix(glm::mat4 m0, glm::mat4 m1, float epsilon = 0.0) {
  bool compare = true;
  for (uint32_t i = 0; i < 4; i++) {
    for (uint32_t j = 0; j < 4; j++) {
      compare = compare && CompareFloat(m0[i][j], m1[i][j], epsilon);
    }
  }

  if (!compare) {
    FXL_LOG(WARNING) << "The following matrices differ:\n";
    PrintMatrix(m0);
    PrintMatrix(m1);
  }
  return compare;
}

glm::mat4 MatrixFromPose(const hmd::Pose& pose) {
  return glm::toMat4(glm::quat(pose.d, pose.a, pose.b, pose.c)) *
         glm::translate(mat4(), glm::vec3(pose.x, pose.y, pose.z));
}

VK_TEST(PoseBuffer, ComputeShaderLatching) {
  // Initialize Vulkan.
  escher::VulkanInstance::Params instance_params(
      {{},
       {VK_EXT_DEBUG_REPORT_EXTENSION_NAME, VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_MAGMA_SURFACE_EXTENSION_NAME},
       true});

// Only enable Vulkan validation layers when in debug mode.
#if !defined(NDEBUG)
  instance_params.layer_names.insert("VK_LAYER_LUNARG_standard_validation");
#endif

  auto vulkan_instance =
      escher::VulkanInstance::New(std::move(instance_params));
  auto vulkan_device = escher::VulkanDeviceQueues::New(vulkan_instance, {});

  auto escher = std::make_unique<escher::Escher>(vulkan_device);
  escher::FramePtr frame = escher->NewFrame("PoseBufferLatchingTest");

  uint32_t num_entries = 8;
  uint64_t base_time = zx::clock::get(ZX_CLOCK_MONOTONIC).get();
  uint64_t time_interval = 1024 * 1024;  // 1 ms

  vk::DeviceSize pose_buffer_size = num_entries * sizeof(escher::hmd::Pose);
  vk::MemoryPropertyFlags memory_property_flags =
      vk::MemoryPropertyFlagBits::eHostVisible |
      vk::MemoryPropertyFlagBits::eHostCoherent;
  vk::BufferUsageFlags buffer_usage_flags =
      vk::BufferUsageFlagBits::eUniformBuffer |
      vk::BufferUsageFlagBits::eStorageBuffer;

  // Create the shader.
  hmd::PoseBuffer pose_buffer(
      escher::Buffer::New(escher->resource_recycler(), frame->gpu_allocator(),
                          pose_buffer_size, buffer_usage_flags,
                          memory_property_flags),
      num_entries, base_time, time_interval);

  hmd::PoseBufferLatchingShader test_shader(escher.get());

  // Fill the pose buffer.
  ASSERT_NE(nullptr, pose_buffer.buffer->ptr());
  hmd::Pose* poses = reinterpret_cast<hmd::Pose*>(pose_buffer.buffer->ptr());
  float pi = glm::pi<float>();
  for (uint32_t i = 0; i < num_entries; i++) {
    // Change pose each interation. The goal is to have unique poses in each
    // slot of the buffer where the first pose is the identity pose.
    glm::vec3 pos = vec3(i * 3.f, i * 5.f, i * 7.f);
    vec3 euler_angles(2 * pi / num_entries * i);
    glm::quat quat(euler_angles);
    new (&poses[i]) escher::hmd::Pose(quat, pos);
  }

  // Dispatch shaders.
  std::vector<BufferPtr> output_buffers;
  std::vector<Camera> cameras;
  // Dispatch a few extra to test modulo rollover.
  uint32_t num_dispatches = num_entries * 2;
  for (uint32_t i = 0; i < num_dispatches; i++) {
    // Identity Camera.
    Camera camera(glm::mat4(1), glm::mat4(1));
    // Use identity camera for first iteration only, change for all others.
    if (i != 0) {
      camera = Camera(
          glm::rotate(glm::mat4(), 2 * pi / num_dispatches * i, vec3(1, 1, 1)),
          glm::perspective(45.0f, 1.0f, 0.1f, 100.0f));
    }

    uint64_t latch_time = base_time + (time_interval * (0.5 + i));
    output_buffers.push_back(
        test_shader.LatchPose(frame, camera, pose_buffer, latch_time, true));
    cameras.push_back(camera);
  }

  // Execute shaders.
  frame->EndFrame(nullptr, []() {});
  auto result = escher->vk_device().waitIdle();
  ASSERT_EQ(vk::Result::eSuccess, result);

  // Verify results.
  for (uint32_t i = 0; i < num_dispatches; i++) {
    auto output_buffer = output_buffers[i];
    ASSERT_NE(nullptr, output_buffer->ptr());
    uint32_t index = i % num_entries;
    auto pose_in = &poses[index];
    auto pose_out = reinterpret_cast<hmd::Pose*>(output_buffer->ptr());
    EXPECT_TRUE(ComparePose(pose_in, pose_out, 0.0));
    glm::mat4 vp_matrix_in = cameras[i].projection() *
                             MatrixFromPose(*pose_in) * cameras[i].transform();
    glm::mat4 vp_matrix_out = glm::make_mat4(
        reinterpret_cast<float*>(output_buffer->ptr() + sizeof(hmd::Pose)));
    EXPECT_TRUE(CompareMatrix(vp_matrix_in, vp_matrix_out, 0.00001));

    // Pose zero uses all identity params so VP result should be identity.
    if (i == 0) {
      EXPECT_TRUE(CompareMatrix(mat4(), vp_matrix_out, 0.0));
    }
  }

  escher->Cleanup();
}
}
}