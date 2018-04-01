// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <deque>
#include <mutex>

#include "garnet/bin/media/framework/models/active_source.h"
#include "garnet/bin/media/framework/stages/stage_impl.h"
#include "lib/fxl/synchronization/thread_annotations.h"

namespace media {

// A stage that hosts an ActiveSource.
class ActiveSourceStageImpl : public StageImpl, public ActiveSourceStage {
 public:
  ActiveSourceStageImpl(std::shared_ptr<ActiveSource> source);

  ~ActiveSourceStageImpl() override;

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
  // ActiveSourceStage implementation.
  void SetTaskRunner(fxl::RefPtr<fxl::TaskRunner> task_runner) override;

  void PostTask(const fxl::Closure& task) override;

  void SupplyPacket(PacketPtr packet) override;

  Output output_;
  std::shared_ptr<ActiveSource> source_;
  bool prepared_;

  mutable std::mutex mutex_;
  std::deque<PacketPtr> packets_ FXL_GUARDED_BY(mutex_);
};

}  // namespace media
