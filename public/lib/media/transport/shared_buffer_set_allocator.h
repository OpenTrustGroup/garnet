// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

#include "lib/fxl/logging.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/media/transport/fifo_allocator.h"
#include "lib/media/transport/shared_buffer_set.h"

namespace media {

// SharedBufferSetAllocator enhances SharedBufferSet by adding allocation
// semantics (allocate and release). SharedBufferSetAllocator is thread-safe.
//
// DESIGN:
//
// SharedBufferSetAllocator implements two different strategies depending on
// the size of the allocations requested. In most media applications, only one
// of the two strategies will be used for any given instance, but both
// strategies can be used at the same time.
//
// The 'sliced' strategy is used for smaller allocations (smaller than
// kWholeRegionMinimumSize). In this strategy, there is one 'active' shared
// buffer from which allocations are made. If the active buffer proves
// insufficient, a larger one is created, and that one becomes active. Buffers
// that were previously active are deleted once all of their allocations have
// been released.
//
// The size chosen for the initial active buffer is a function of the first
// allocation request. Specifically, it's the size of the allocation request
// multipled by kSlicedBufferInitialSizeMultiplier. When an existing active
// buffer is too small and a new one needs to be created, the new size is either
// kSlicedBufferGrowMultiplier times the old size or
// kSlicedBufferInitialSizeMultiplier times the current request size, whichever
// is larger.
//
// The 'whole' strategy is used for larger allocations (at least as large as
// kWholeRegionMinimumSize). In this strategy, each allocation uses a whole
// shared buffer. A list of free whole buffers is maintained so buffers are
// reused. When an allocation occurs, free whole buffers that are too small
// are deleted. If none of the free whole buffers is large enough, a new
// buffer of the required size is allocated.
//
// This design is intended to do a reasonable job of serving the real-world
// needs of the fidl packet transport. The 'whole' strategy is intended for the
// very large packets containing uncompressed video. Typically, these are of
// consistent size for a given connection, and packet flow control is tuned to
// keep their number small and consistent (typically one or two per connection).
//
// The 'sliced' strategy is intended for packets containing anything but
// uncompressed video. Such packets are rarely bigger than a few kilobytes, and
// their size and number per connection may be more varied.
//
// Both strategies, relative to their intended uses, are designed to be
// reasonably memory-efficient and to reasonably minimize the number of
// buffers and the rate at which buffers are created and deleted. Another
// consideration is the complexity of this code. Additional logic may be added
// later if the additional complexity is justified by improved efficiency.
//
// This code can be tuned by tweaking the various constants:
//
// kWholeRegionMinimumSize: This value should be tuned such that uncompressed
// video packets fall above this value and other packets fall below. So far,
// observations bear out the assumption that packet sizes are clustered well
// away from this threshold (much higher for uncompressed video packets, much
// lower for other packets).
//
// kSlicedBufferInitialSizeMultiplier: This value should be tuned to maximize
// the sliced buffer utilization while minimizing the chances that a new buffer
// will need to be allocated.
//
// kSlicedBufferGrowMultiplier: This value should be tuned to maximimize the
// utilization of replacement buffers while minimizing the chances that a new
// buffer will need to be allocated yet again.
//
// The intended use for this class is media packet prodcuer implementations
// (such as MediaPacketProducerBase and its subclasses). Producers need to
// inform consumers of changes to the set of buffers used for the connection.
// The producer accomplishes this by calling PollForBufferUpdate and sending
// the relevant AddPayloadBuffer and RemovePayloadBuffer messages. The queue
// of updates should be drained before the producer sends a SupplyPacket
// message to assure that the buffer referenced by SupplyPacket is known to the
// consumer.
//
// MediaPacketConsumer implementations such as MediaPacketConsumerBase and its
// subclasses should used SharedBufferSet rather than SharedBufferSetAllocator,
// because they only need the buffer mapping and offset translation
// functionality it provides, not the ability to allocate and release regions.
//
class SharedBufferSetAllocator : public SharedBufferSet {
 public:
  // Constructs a SharedBufferSetAllocator. |local_map_flags| specifies flags
  // used to map vmos for local access. |remote_rights| specifies the rights
  // applies to vmos sent to the remote party via buffer updates.
  SharedBufferSetAllocator(uint32_t local_map_flags, zx_rights_t remote_rights);

  ~SharedBufferSetAllocator() override;

  // Resets the object to its initial state.
  void Reset() override;

  // Configures the allocator to use a single buffer of the specified size for
  // all allocations. |AllocateRegion| fails if the requested allocation cannot
  // be accommodated using the single VMO. This method should be called before
  // any allocation attempts. Returns true unless the VMO could not be
  // allocated.
  bool SetFixedBufferSize(uint64_t size);

  // Allocates a region, returning a pointer. If the requested region could not
  // be allocated, returns nullptr.
  void* AllocateRegion(uint64_t size);

  // Releases a region of the buffer previously allocated.
  void ReleaseRegion(void* ptr);

  // Returns the first queued buffer update, if there is one. Return true if
  // there is one, false if not. If this method returns true, |*buffer_id_out|
  // and |*handle_out| are updated. If |*handle_out| is valid, the update is
  // a buffer add, a buffer remove if not.
  bool PollForBufferUpdate(uint32_t* buffer_id_out, zx::vmo* handle_out);

 private:
  struct Buffer {
    Buffer();

    Buffer(Buffer&& other) {
      size_ = other.size_;
      allocator_ = std::move(other.allocator_);
    }

    ~Buffer();

    bool whole() { return !static_cast<bool>(allocator_); }

    uint64_t size_;
    std::unique_ptr<FifoAllocator> allocator_;
  };

  struct BufferUpdate {
    BufferUpdate(uint32_t buffer_id, zx::vmo vmo);

    BufferUpdate(uint32_t buffer_id);

    ~BufferUpdate();

    uint32_t buffer_id_;
    zx::vmo vmo_;
  };

  static constexpr uint64_t kWholeRegionMinimumSize = 64 * 1024;
  static constexpr uint64_t kSlicedBufferInitialSizeMultiplier = 3;
  static constexpr uint64_t kSlicedBufferGrowMultiplier = 2;
  static constexpr uint32_t kNullBufferId =
      std::numeric_limits<uint32_t>::max();

  // Allocates a region using the whole region strategy.
  Locator AllocateWholeRegion(uint64_t size)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Releases a region using the whole region strategy.
  void ReleaseWholeRegion(const Locator& locator)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Allocates a region using the sliced region strategy.
  Locator AllocateSlicedRegion(uint64_t size)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Releases a region using the sliced region strategy.
  void ReleaseSlicedRegion(const Locator& locator)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Creates a new buffer and notifies the caller that this has occurred.
  uint32_t CreateBuffer(bool whole, uint64_t size)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Deletes a buffer and notifies the caller that this has occurred.
  void DeleteBuffer(uint32_t id) FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Deletes the buffer if it's fully released.
  void MaybeDeleteSlicedBuffer(uint32_t id)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  zx_rights_t remote_rights_;
  mutable std::mutex mutex_;
  bool use_fixed_buffer_ FXL_GUARDED_BY(mutex_) = false;
  std::vector<Buffer> buffers_ FXL_GUARDED_BY(mutex_);
  std::multimap<uint64_t, uint32_t> free_whole_buffer_ids_by_size_
      FXL_GUARDED_BY(mutex_);
  uint32_t active_sliced_buffer_id_ FXL_GUARDED_BY(mutex_) = kNullBufferId;
  std::queue<BufferUpdate> buffer_updates_ FXL_GUARDED_BY(mutex_);
};

}  // namespace media
