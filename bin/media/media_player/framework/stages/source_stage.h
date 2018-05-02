// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_STAGES_SOURCE_STAGE_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_STAGES_SOURCE_STAGE_H_

#include <deque>
#include <mutex>

#include "garnet/bin/media/media_player/framework/models/source.h"
#include "garnet/bin/media/media_player/framework/stages/stage_impl.h"
#include "lib/fxl/synchronization/thread_annotations.h"

namespace media_player {

// A stage that hosts an Source.
class SourceStageImpl : public StageImpl, public SourceStage {
 public:
  SourceStageImpl(std::shared_ptr<Source> source);

  ~SourceStageImpl() override;

  // StageImpl implementation.
  size_t input_count() const override;

  Input& input(size_t index) override;

  size_t output_count() const override;

  Output& output(size_t index) override;

  std::shared_ptr<PayloadAllocator> PrepareInput(size_t index) override;

  void PrepareOutput(size_t index,
                     std::shared_ptr<PayloadAllocator> allocator,
                     UpstreamCallback callback) override;

  void UnprepareOutput(size_t index, UpstreamCallback callback) override;

  void FlushInput(size_t index,
                  bool hold_frame,
                  DownstreamCallback callback) override;

  void FlushOutput(size_t index) override;

 protected:
  // StageImpl implementation.
  GenericNode* GetGenericNode() override;

  void Update() override;

 private:
  // SourceStage implementation.
  void PostTask(const fxl::Closure& task) override;

  void SupplyPacket(PacketPtr packet) override;

  Output output_;
  std::shared_ptr<Source> source_;
  bool prepared_;

  mutable std::mutex mutex_;
  std::deque<PacketPtr> packets_ FXL_GUARDED_BY(mutex_);
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_STAGES_SOURCE_STAGE_H_
