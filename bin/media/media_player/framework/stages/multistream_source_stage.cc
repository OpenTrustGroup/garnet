// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/framework/stages/multistream_source_stage.h"

#include "garnet/bin/media/media_player/framework/stages/util.h"

namespace media_player {

MultistreamSourceStageImpl::MultistreamSourceStageImpl(
    std::shared_ptr<MultistreamSource> source)
    : packets_per_output_(source->stream_count()), source_(source) {
  FXL_DCHECK(source);

  for (size_t index = 0; index < source->stream_count(); ++index) {
    outputs_.emplace_back(this, index);
  }
}

MultistreamSourceStageImpl::~MultistreamSourceStageImpl() {}

size_t MultistreamSourceStageImpl::input_count() const {
  return 0;
};

Input& MultistreamSourceStageImpl::input(size_t index) {
  FXL_CHECK(false) << "input requested from source";
  abort();
}

size_t MultistreamSourceStageImpl::output_count() const {
  return outputs_.size();
}

Output& MultistreamSourceStageImpl::output(size_t index) {
  FXL_DCHECK(index < outputs_.size());
  return outputs_[index];
}

std::shared_ptr<PayloadAllocator> MultistreamSourceStageImpl::PrepareInput(
    size_t index) {
  FXL_CHECK(false) << "PrepareInput called on source";
  return nullptr;
}

void MultistreamSourceStageImpl::PrepareOutput(
    size_t index,
    std::shared_ptr<PayloadAllocator> allocator,
    UpstreamCallback callback) {
  FXL_DCHECK(index < outputs_.size());

  if (allocator != nullptr) {
    // Currently, we don't support a source that uses provided allocators. If
    // we're provided an allocator, the output must have it so supplied packets
    // can be copied.
    outputs_[index].SetCopyAllocator(allocator);
  }
}

void MultistreamSourceStageImpl::UnprepareOutput(size_t index,
                                                 UpstreamCallback callback) {
  FXL_DCHECK(index < outputs_.size());
  outputs_[index].SetCopyAllocator(nullptr);
}

GenericNode* MultistreamSourceStageImpl::GetGenericNode() {
  return source_.get();
}

void MultistreamSourceStageImpl::Update() {
  fbl::AutoLock lock(&mutex_);

  FXL_DCHECK(outputs_.size() == packets_per_output_.size());

  bool need_packet = false;

  for (size_t i = 0; i < outputs_.size(); i++) {
    Output& output = outputs_[i];

    if (!output.connected()) {
      continue;
    }

    std::deque<PacketPtr>& packets = packets_per_output_[i];

    if (packets.empty()) {
      if (output.demand() == Demand::kPositive) {
        // The output has positive demand and no packets queued. Request another
        // packet so we can meet the demand.
        need_packet = true;
      }
    } else if (output.demand() != Demand::kNegative) {
      // The output has non-negative demand and packets queued. Send a packet
      // downstream now.
      output.SupplyPacket(std::move(packets.front()));
      packets.pop_front();
    }
  }

  if (need_packet && !packet_request_outstanding_) {
    packet_request_outstanding_ = true;
    source_->RequestPacket();
  }
}

void MultistreamSourceStageImpl::FlushInput(size_t index,
                                            bool hold_frame,
                                            DownstreamCallback callback) {
  FXL_CHECK(false) << "FlushInput called on source";
}

void MultistreamSourceStageImpl::FlushOutput(size_t index) {
  fbl::AutoLock lock(&mutex_);
  FXL_DCHECK(index < outputs_.size());
  FXL_DCHECK(source_);
  packets_per_output_[index].clear();
  ended_streams_ = 0;
  packet_request_outstanding_ = false;
}

void MultistreamSourceStageImpl::PostTask(const fxl::Closure& task) {
  StageImpl::PostTask(task);
}

void MultistreamSourceStageImpl::SupplyPacket(size_t output_index,
                                              PacketPtr packet) {
  fbl::AutoLock lock(&mutex_);
  FXL_DCHECK(output_index < outputs_.size());
  FXL_DCHECK(outputs_.size() == packets_per_output_.size());
  FXL_DCHECK(packet);

  if (!packet_request_outstanding_) {
    // We requested a packet, then changed our minds due to a flush. Discard
    // the packet.
    return;
  }

  packet_request_outstanding_ = false;

  if (packet->end_of_stream()) {
    ++ended_streams_;
  }

  if (!outputs_[output_index].connected()) {
    // The output in question isn't connected. Discard the packet and request
    // another.
    source_->RequestPacket();
    packet_request_outstanding_ = true;
    return;
  }

  // We put new packets in per-output (per-stream) queues. That way, when
  // we get a bunch of undemanded packets for a particular stream, we can
  // queue them up here until they're demanded.
  std::deque<PacketPtr>& packets = packets_per_output_[output_index];
  packets.push_back(std::move(packet));

  if (packets.size() == 1 &&
      outputs_[output_index].demand() != Demand::kNegative) {
    // We have a packet for an output with non-negative demand that didn't
    // have one before. Request an update. Update will request another
    // packet, if needed.
    lock.release();
    NeedsUpdate();
  } else {
    // We got a packet, but it doesn't change matters, either because the
    // output in question already had a packet queued or because that output
    // has negative demand and wasn't the one we wanted a packet for.
    // We can request another packet without having to go through an update.
    source_->RequestPacket();
    packet_request_outstanding_ = true;
  }
}

}  // namespace media_player
