// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/framework/stages/multistream_sink_stage.h"

namespace media_player {

MultistreamSinkStageImpl::MultistreamSinkStageImpl(
    std::shared_ptr<MultistreamSink> sink)
    : sink_(sink) {
  FXL_DCHECK(sink_);
  // Add one unallocated input so this stage isn't misidentified as a source.
  ReleaseInput(AllocateInput());
}

MultistreamSinkStageImpl::~MultistreamSinkStageImpl() {}

size_t MultistreamSinkStageImpl::input_count() const {
  // TODO(dalesat): Provide checks to make sure inputs_.size() is stable when
  // it needs to be.
  std::lock_guard<std::mutex> locker(mutex_);
  return inputs_.size();
};

Input& MultistreamSinkStageImpl::input(size_t index) {
  std::lock_guard<std::mutex> locker(mutex_);
  FXL_DCHECK(index < inputs_.size());
  return inputs_[index]->input_;
}

size_t MultistreamSinkStageImpl::output_count() const {
  return 0;
}

Output& MultistreamSinkStageImpl::output(size_t index) {
  FXL_CHECK(false) << "output requested from sink";
  abort();
}

std::shared_ptr<PayloadAllocator> MultistreamSinkStageImpl::PrepareInput(
    size_t index) {
  return nullptr;
}

void MultistreamSinkStageImpl::PrepareOutput(
    size_t index,
    std::shared_ptr<PayloadAllocator> allocator,
    UpstreamCallback callback) {
  FXL_CHECK(false) << "PrepareOutput called on sink";
}

GenericNode* MultistreamSinkStageImpl::GetGenericNode() {
  return sink_.get();
}

void MultistreamSinkStageImpl::Update() {
  FXL_DCHECK(sink_);

  std::lock_guard<std::mutex> locker(mutex_);

  for (auto iter = pending_inputs_.begin(); iter != pending_inputs_.end();) {
    FXL_DCHECK(*iter < inputs_.size());
    StageInput* input = inputs_[*iter].get();
    if (input->input_.packet()) {
      input->demand_ = sink_->SupplyPacket(
          input->input_.index(), input->input_.TakePacket(Demand::kNegative));

      if (input->demand_ == Demand::kNegative) {
        auto remove_iter = iter;
        ++iter;
        pending_inputs_.erase(remove_iter);
      }
    } else {
      ++iter;
    }

    input->input_.SetDemand(input->demand_);
  }
}

void MultistreamSinkStageImpl::FlushInput(size_t index,
                                          bool hold_frame,
                                          DownstreamCallback callback) {
  FXL_DCHECK(sink_);

  sink_->Flush(hold_frame);

  std::lock_guard<std::mutex> locker(mutex_);
  inputs_[index]->input_.Flush();

  pending_inputs_.remove(index);
}

void MultistreamSinkStageImpl::FlushOutput(size_t index) {
  FXL_CHECK(false) << "FlushOutput called on sink";
}

void MultistreamSinkStageImpl::PostTask(const fxl::Closure& task) {
  StageImpl::PostTask(task);
}

size_t MultistreamSinkStageImpl::AllocateInput() {
  std::lock_guard<std::mutex> locker(mutex_);

  StageInput* input;
  if (unallocated_inputs_.empty()) {
    input = new StageInput(this, inputs_.size());
    inputs_.emplace_back(std::unique_ptr<StageInput>(input));
  } else {
    // Allocate lowest indices first.
    auto iter = unallocated_inputs_.lower_bound(0);
    input = inputs_[*iter].get();
    FXL_DCHECK(!input->allocated_);
    unallocated_inputs_.erase(iter);
  }

  input->allocated_ = true;

  return input->input_.index();
}

size_t MultistreamSinkStageImpl::ReleaseInput(size_t index) {
  std::lock_guard<std::mutex> locker(mutex_);
  FXL_DCHECK(index < inputs_.size());

  StageInput* input = inputs_[index].get();
  FXL_DCHECK(input);
  FXL_DCHECK(input->allocated_);
  FXL_DCHECK(!input->input_.connected());

  input->allocated_ = false;

  // Pop input if it's at the end of inputs_. Otherwise, add it to
  // unallocated_inputs_. We never pop the last input so the stage can't be
  // misidentified as a source.
  if (index != 0 && index == inputs_.size() - 1) {
    while (inputs_.size() > 1 && !inputs_.back()->allocated_) {
      unallocated_inputs_.erase(inputs_.size() - 1);
      inputs_.pop_back();
    }
  } else {
    unallocated_inputs_.insert(input->input_.index());
  }

  return inputs_.size();
}

void MultistreamSinkStageImpl::UpdateDemand(size_t input_index, Demand demand) {
  {
    std::lock_guard<std::mutex> locker(mutex_);
    FXL_DCHECK(input_index < inputs_.size());
    FXL_DCHECK(demand != Demand::kNegative);

    StageInput* input = inputs_[input_index].get();
    FXL_DCHECK(input);
    input->demand_ = demand;
    pending_inputs_.push_back(input_index);
  }

  NeedsUpdate();
}

}  // namespace media_player
