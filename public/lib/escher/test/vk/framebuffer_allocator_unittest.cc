// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/escher/test/gtest_escher.h"

#include <vector>

#include "garnet/public/lib/escher/resources/resource_recycler.h"
#include "garnet/public/lib/escher/third_party/granite/vk/render_pass.h"
#include "garnet/public/lib/escher/vk/impl/framebuffer.h"
#include "garnet/public/lib/escher/vk/impl/framebuffer_allocator.h"
#include "garnet/public/lib/escher/vk/impl/render_pass_cache.h"
#include "garnet/public/lib/escher/vk/render_pass_info.h"
#include "garnet/public/lib/escher/vk/texture.h"

namespace {
using namespace escher;

struct FramebufferTextures {
  TexturePtr color1;
  TexturePtr color2;
  TexturePtr depth;
};

std::vector<FramebufferTextures> MakeFramebufferTextures(
    Escher* escher, size_t count, uint32_t width, uint32_t height,
    uint32_t sample_count, vk::Format color_format1, vk::Format color_format2,
    vk::Format depth_format) {
  std::vector<FramebufferTextures> result;
  result.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    FramebufferTextures textures;
    if (color_format1 != vk::Format::eUndefined) {
      textures.color1 = escher->NewAttachmentTexture(
          color_format1, width, height, 1, vk::Filter::eNearest);
    }
    if (color_format2 != vk::Format::eUndefined) {
      textures.color2 = escher->NewAttachmentTexture(
          color_format2, width, height, 1, vk::Filter::eNearest);
    }
    if (depth_format != vk::Format::eUndefined) {
      textures.depth = escher->NewAttachmentTexture(depth_format, width, height,
                                                    1, vk::Filter::eNearest);
    }
    result.push_back(std::move(textures));
  }
  return result;
}

RenderPassInfo MakeRenderPassInfo(const FramebufferTextures& textures) {
  RenderPassInfo info;
  info.num_color_attachments = 0;
  if (textures.color1) {
    info.color_attachments[info.num_color_attachments++] = textures.color1;
  }
  if (textures.color2) {
    info.color_attachments[info.num_color_attachments++] = textures.color2;
  }
  info.depth_stencil_attachment = textures.depth;
  return info;
}

std::vector<impl::FramebufferPtr> ObtainFramebuffers(
    impl::FramebufferAllocator* allocator,
    const std::vector<FramebufferTextures>& textures) {
  std::vector<impl::FramebufferPtr> result;
  result.reserve(textures.size());
  for (auto& texs : textures) {
    result.push_back(allocator->ObtainFramebuffer(MakeRenderPassInfo(texs)));
  }
  return result;
}

VK_TEST(FramebufferAllocator, Basic) {
  auto escher = test::GetEscher();

  impl::RenderPassCache cache(escher->resource_recycler());
  impl::FramebufferAllocator allocator(escher->resource_recycler(), &cache);
  allocator.BeginFrame();

  uint32_t width = 1024;
  uint32_t height = 1024;

  // Create a pair of each of three types of framebuffers.
  auto textures_2colors_D24 = MakeFramebufferTextures(
      escher, 2, width, height, 1, vk::Format::eB8G8R8A8Unorm,
      vk::Format::eB8G8R8A8Unorm, vk::Format::eD24UnormS8Uint);
  auto textures_2colors_D32 = MakeFramebufferTextures(
      escher, 2, width, height, 1, vk::Format::eB8G8R8A8Unorm,
      vk::Format::eB8G8R8A8Unorm, vk::Format::eD32SfloatS8Uint);
  auto textures_1color_D32 = MakeFramebufferTextures(
      escher, 2, width, height, 1, vk::Format::eB8G8R8A8Unorm,
      vk::Format::eUndefined, vk::Format::eD32SfloatS8Uint);

  auto framebuffers_2colors_D24 =
      ObtainFramebuffers(&allocator, textures_2colors_D24);
  auto framebuffers_2colors_D32 =
      ObtainFramebuffers(&allocator, textures_2colors_D32);
  auto framebuffers_1color_D32 =
      ObtainFramebuffers(&allocator, textures_1color_D32);

  // Each pair should have two different Framebuffers which share the same
  // RenderPass.
  EXPECT_NE(framebuffers_2colors_D24[0], framebuffers_2colors_D24[1]);
  EXPECT_EQ(framebuffers_2colors_D24[0]->render_pass(),
            framebuffers_2colors_D24[1]->render_pass());
  EXPECT_NE(framebuffers_2colors_D32[0], framebuffers_2colors_D32[1]);
  EXPECT_EQ(framebuffers_2colors_D32[0]->render_pass(),
            framebuffers_2colors_D32[1]->render_pass());
  EXPECT_NE(framebuffers_1color_D32[0], framebuffers_1color_D32[1]);
  EXPECT_EQ(framebuffers_1color_D32[0]->render_pass(),
            framebuffers_1color_D32[1]->render_pass());

  // Each pair of Framebuffers should have a different RenderPass from the other
  // pairs.
  EXPECT_EQ(cache.size(), 3U);
  EXPECT_NE(framebuffers_2colors_D24[0]->render_pass(),
            framebuffers_2colors_D32[0]->render_pass());
  EXPECT_NE(framebuffers_2colors_D24[0]->render_pass(),
            framebuffers_1color_D32[0]->render_pass());
  EXPECT_NE(framebuffers_2colors_D32[0]->render_pass(),
            framebuffers_1color_D32[0]->render_pass());
}

VK_TEST(FramebufferAllocator, CacheReclamation) {
  auto escher = test::GetEscher();

  impl::RenderPassCache cache(escher->resource_recycler());
  impl::FramebufferAllocator allocator(escher->resource_recycler(), &cache);
  allocator.BeginFrame();

  uint32_t width = 1024;
  uint32_t height = 1024;

  // Make a single set of textures (depth and 2 color attachments) that will be
  // used to make a framebuffer.
  auto textures = MakeFramebufferTextures(
      escher, 1, width, height, 1, vk::Format::eB8G8R8A8Unorm,
      vk::Format::eB8G8R8A8Unorm, vk::Format::eD24UnormS8Uint);
  auto framebuffer = ObtainFramebuffers(&allocator, textures);

  // Obtaining a Framebuffer using the same textures should result in the same
  // Framebuffer.
  EXPECT_EQ(framebuffer, ObtainFramebuffers(&allocator, textures));

  // ... this should still be true on the following frame.
  allocator.BeginFrame();
  EXPECT_EQ(framebuffer, ObtainFramebuffers(&allocator, textures));

  // ... in fact, Framebuffers should not be evicted from the cache as long as
  // the number of frames since last use is < kFramesUntilEviction.
  constexpr uint32_t kNotEnoughFramesForEviction =
      impl::FramebufferAllocator::kFramesUntilEviction;
  for (uint32_t i = 0; i < kNotEnoughFramesForEviction; ++i) {
    allocator.BeginFrame();
  }
  EXPECT_EQ(framebuffer, ObtainFramebuffers(&allocator, textures));

  // ... but one more frame than that will cause a different Framebuffer to be
  // obtained from the allocator.
  constexpr uint32_t kJustEnoughFramesForEviction =
      kNotEnoughFramesForEviction + 1;
  for (uint32_t i = 0; i < kJustEnoughFramesForEviction; ++i) {
    allocator.BeginFrame();
  }
  EXPECT_NE(framebuffer, ObtainFramebuffers(&allocator, textures));
}

}  // anonymous namespace
