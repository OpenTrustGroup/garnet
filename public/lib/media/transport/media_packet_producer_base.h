// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MEDIA_TRANSPORT_MEDIA_PACKET_PRODUCER_BASE_H_
#define LIB_MEDIA_TRANSPORT_MEDIA_PACKET_PRODUCER_BASE_H_

#include <limits>
#include <mutex>

#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/fxl/synchronization/thread_checker.h"
#include <media/cpp/fidl.h>
#include "lib/media/timeline/timeline_rate.h"
#include "lib/media/transport/shared_buffer_set_allocator.h"

namespace media {

// Base class for clients of MediaPacketConsumer.
class MediaPacketProducerBase {
 public:
  using ProducePacketCallback = std::function<void()>;

  MediaPacketProducerBase();

  virtual ~MediaPacketProducerBase();

  // Allocates a single vmo of the specified size for all buffer allocations.
  // Must be called before the first call to |AllocatePayloadBuffer|.
  void SetFixedBufferSize(uint64_t size);

  // Connects to the indicated consumer.
  void Connect(MediaPacketConsumerPtr consumer,
               const MediaPacketProducer::ConnectCallback& callback);

  // Disconnects from the consumer.
  void Disconnect();

  // Determines if we are connected to a consumer.
  bool is_connected() { return consumer_.is_bound(); }

  // Resets to initial state.
  void Reset();

  // Flushes the consumer.
  void FlushConsumer(bool hold_frame,
                     const MediaPacketConsumer::FlushCallback& callback);

  // Allocates a payload buffer of the specified size.
  void* AllocatePayloadBuffer(size_t size);

  // Releases a payload buffer obtained via AllocatePayloadBuffer.
  void ReleasePayloadBuffer(void* buffer);

  // Produces a packet and supplies it to the consumer.
  void ProducePacket(void* payload,
                     size_t size,
                     int64_t pts,
                     TimelineRate pts_rate,
                     bool keyframe,
                     bool end_of_stream,
                     MediaTypePtr revised_media_type,
                     const ProducePacketCallback& callback);

  // Gets the current demand.
  const MediaPacketDemand& demand() const { return demand_; }

  // Determines whether the consumer is currently demanding a packet. The
  // |additional_packets_outstanding| parameter indicates the number of packets
  // that should be added to the current outstanding packet count when
  // determining demand. For example, a value of 1 means that the function
  // should determine demand as if one additional packet was outstanding.
  bool ShouldProducePacket(uint32_t additional_packets_outstanding);

 protected:
  // Called when demand is updated. If demand is updated in a SupplyPacket
  // callback, the DemandUpdatedCallback is called before the
  // ProducePacketCallback.
  // TODO(dalesat): Default implementation?
  virtual void OnDemandUpdated(uint32_t min_packets_outstanding,
                               int64_t min_pts) = 0;

  // Called when a fatal error occurs. The default implementation does nothing.
  virtual void OnFailure();

 private:
  // Initializes demand to 0.
  void ResetDemand();

  // Handles a demand update callback or, if called with default parameters,
  // initiates demand update requests.
  void HandleDemandUpdate(MediaPacketDemandPtr demand = nullptr);

  // Updates demand_ and calls demand_update_callback_ if it's set and demand
  // has changed.
  void UpdateDemand(const MediaPacketDemand& demand);

  SharedBufferSetAllocator allocator_;
  MediaPacketConsumerPtr consumer_;
  bool flush_in_progress_ = false;

  mutable std::mutex mutex_;
  MediaPacketDemand demand_ FXL_GUARDED_BY(mutex_);
  uint32_t packets_outstanding_ FXL_GUARDED_BY(mutex_);
  int64_t pts_last_produced_ FXL_GUARDED_BY(mutex_) =
      std::numeric_limits<int64_t>::min();
  bool end_of_stream_ FXL_GUARDED_BY(mutex_) = false;

  FXL_DECLARE_THREAD_CHECKER(thread_checker_);
};

}  // namespace media

#endif  // LIB_MEDIA_TRANSPORT_MEDIA_PACKET_PRODUCER_BASE_H_
