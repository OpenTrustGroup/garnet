// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_STAGES_MULTISTREAM_SOURCE_STAGE_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_STAGES_MULTISTREAM_SOURCE_STAGE_H_

#include <deque>
#include <vector>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "garnet/bin/media/media_player/framework/models/multistream_source.h"
#include "garnet/bin/media/media_player/framework/stages/stage_impl.h"
#include "lib/fxl/synchronization/thread_annotations.h"

namespace media_player {

// A stage that hosts an MultistreamSource.
class MultistreamSourceStageImpl : public StageImpl,
                                   public MultistreamSourceStage {
 public:
  MultistreamSourceStageImpl(std::shared_ptr<MultistreamSource> source);

  ~MultistreamSourceStageImpl() override;

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
  // MultistreamSourceStage implementation.
  void PostTask(const fxl::Closure& task) override;

  void SupplyPacket(size_t output_index, PacketPtr packet) override;

  std::vector<Output> outputs_;
  std::vector<std::deque<PacketPtr>> packets_per_output_;
  std::shared_ptr<MultistreamSource> source_;

  mutable fbl::Mutex mutex_;
  size_t ended_streams_ FXL_GUARDED_BY(mutex_) = 0;
  bool packet_request_outstanding_ FXL_GUARDED_BY(mutex_) = false;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_STAGES_MULTISTREAM_SOURCE_STAGE_H_
