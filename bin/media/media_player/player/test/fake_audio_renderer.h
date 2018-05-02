// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_PLAYER_TEST_FAKE_AUDIO_RENDERER_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_PLAYER_TEST_FAKE_AUDIO_RENDERER_H_

#include "garnet/bin/media/media_player/render/renderer.h"
#include "lib/fxl/logging.h"

namespace media_player {
namespace test {

class FakeAudioRenderer : public Renderer {
 public:
  static std::shared_ptr<FakeAudioRenderer> Create();

  FakeAudioRenderer();

  ~FakeAudioRenderer() override {}

  const char* label() const override { return "FakeAudioRenderer"; }

  // Renderer implementation.
  void Flush(bool hold_frame) override{};

  std::shared_ptr<PayloadAllocator> allocator() override { return nullptr; }

  Demand SupplyPacket(PacketPtr packet) override { return Demand::kPositive; }

  const std::vector<std::unique_ptr<StreamTypeSet>>& GetSupportedStreamTypes()
      override {
    return supported_stream_types_;
  }

  void SetStreamType(const StreamType& stream_type) override {
    stream_type_ = stream_type.Clone();
  }

  void Prime(fxl::Closure callback) override { callback(); }

  void SetTimelineFunction(media::TimelineFunction timeline_function,
                           fxl::Closure callback) override {
    callback();
  }

  void SetProgramRange(uint64_t program,
                       int64_t min_pts,
                       int64_t max_pts) override {}

 private:
  std::vector<std::unique_ptr<StreamTypeSet>> supported_stream_types_;
  std::unique_ptr<StreamType> stream_type_;
};

}  // namespace test
}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_PLAYER_TEST_FAKE_AUDIO_RENDERER_H_
