// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_STAGES_MULTISTREAM_SINK_STAGE_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_STAGES_MULTISTREAM_SINK_STAGE_H_

#include <list>
#include <mutex>
#include <set>
#include <vector>

#include "garnet/bin/media/media_player/framework/models/multistream_sink.h"
#include "garnet/bin/media/media_player/framework/stages/stage_impl.h"
#include "lib/fxl/synchronization/thread_annotations.h"

namespace media_player {

// A stage that hosts an MultistreamSink.
class MultistreamSinkStageImpl : public StageImpl, public MultistreamSinkStage {
 public:
  MultistreamSinkStageImpl(std::shared_ptr<MultistreamSink> sink);

  ~MultistreamSinkStageImpl() override;

  // StageImpl implementation.
  size_t input_count() const override;

  Input& input(size_t index) override;

  size_t output_count() const override;

  Output& output(size_t index) override;

  std::shared_ptr<PayloadAllocator> PrepareInput(size_t index) override;

  void PrepareOutput(size_t index,
                     std::shared_ptr<PayloadAllocator> allocator,
                     UpstreamCallback callback) override;

  void FlushInput(size_t index,
                  bool hold_frame,
                  DownstreamCallback callback) override;

  void FlushOutput(size_t index) override;

 protected:
  // StageImpl implementation.
  GenericNode* GetGenericNode() override;

  void Update() override;

 private:
  // MultistreamSinkStage implementation.
  void PostTask(const fxl::Closure& task) override;

  size_t AllocateInput() override;

  size_t ReleaseInput(size_t index) override;

  void UpdateDemand(size_t input_index, Demand demand) override;

  struct StageInput {
    StageInput(StageImpl* stage, size_t index)
        : input_(stage, index), allocated_(false), demand_(Demand::kNegative) {}
    Input input_;
    bool allocated_;
    Demand demand_;
  };

  std::shared_ptr<MultistreamSink> sink_;

  mutable std::mutex mutex_;
  std::vector<std::unique_ptr<StageInput>> inputs_ FXL_GUARDED_BY(mutex_);
  std::set<size_t> unallocated_inputs_ FXL_GUARDED_BY(mutex_);
  std::list<size_t> pending_inputs_ FXL_GUARDED_BY(mutex_);
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_STAGES_MULTISTREAM_SINK_STAGE_H_
