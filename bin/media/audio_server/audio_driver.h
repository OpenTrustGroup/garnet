// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <dispatcher-pool/dispatcher-channel.h>
#include <dispatcher-pool/dispatcher-timer.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <zircon/device/audio.h>

#include <mutex>
#include <string>

#include "garnet/bin/media/audio_server/audio_device.h"
#include "garnet/bin/media/audio_server/driver_ring_buffer.h"
#include "garnet/bin/media/audio_server/utils.h"

namespace media {
namespace audio {

class AudioOutput;

class AudioDriver {
 public:
  enum class State {
    Uninitialized,
    Unconfigured,
    Configuring_SettingFormat,
    Configuring_GettingFifoDepth,
    Configuring_GettingRingBuffer,
    Configured,
    Starting,
    Started,
    Stopping,
    Shutdown,
  };

  struct RingBufferSnapshot {
    fbl::RefPtr<DriverRingBuffer> ring_buffer;
    TimelineFunction clock_mono_to_ring_pos_bytes;
    uint32_t position_to_end_fence_frames;
    uint32_t end_fence_to_start_fence_frames;
    uint32_t gen_id;
  };

  AudioDriver(AudioDevice* owner);
  virtual ~AudioDriver() {}

  zx_status_t Init(zx::channel stream_channel);
  void Cleanup();
  void SnapshotRingBuffer(RingBufferSnapshot* snapshot) const;
  AudioMediaTypeDetailsPtr GetSourceFormat() const;

  bool plugged() const {
    fbl::AutoLock lock(&plugged_lock_);
    return plugged_;
  }

  zx_time_t plug_time() const {
    fbl::AutoLock lock(&plugged_lock_);
    return plug_time_;
  }

  // Methods which need to be called from the owner's execution domain.  If
  // there was a good way to use the static lock analysis to ensure this, I
  // would do so, but unfortunately the compiler is unable to figure out that
  // the owner calling these methods is always the same as owner_.
  const std::vector<audio_stream_format_range_t>& format_ranges() const {
    return format_ranges_;
  }

  State state() const { return state_; }
  uint32_t frames_per_sec() const { return frames_per_sec_; }
  uint64_t external_delay_nsec() const { return external_delay_nsec_; }
  uint16_t channel_count() const { return channel_count_; }
  audio_sample_format_t sample_format() const { return sample_format_; }
  uint32_t bytes_per_frame() const { return bytes_per_frame_; }
  uint32_t fifo_depth_bytes() const { return fifo_depth_bytes_; }
  uint32_t fifo_depth_frames() const { return fifo_depth_frames_; }

  void SetEndFenceToStartFenceFrames(uint32_t dist) {
    std::lock_guard<std::mutex> lock(ring_buffer_state_lock_);
    end_fence_to_start_fence_frames_ = dist;
  }

  zx_status_t GetSupportedFormats();
  zx_status_t Configure(uint32_t frames_per_second, uint32_t channels,
                        AudioSampleFormat fmt,
                        zx_duration_t min_ring_buffer_duration);
  zx_status_t Start();
  zx_status_t Stop();
  zx_status_t SetPlugDetectEnabled(bool enabled);

 private:
  friend class AudioDevice;

  // Dispatchers for messages received over stream and ring buffer channels.
  zx_status_t ReadMessage(const fbl::RefPtr<::dispatcher::Channel>& channel,
                          void* buf, uint32_t buf_size,
                          uint32_t* bytes_read_out, zx::handle* handle_out)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain_->token());
  zx_status_t ProcessStreamChannelMessage()
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain_->token());
  zx_status_t ProcessRingBufferChannelMessage()
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain_->token());

  // Stream channel message handlers.
  zx_status_t ProcessGetFormatsResponse(
      const audio_stream_cmd_get_formats_resp_t& resp)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain_->token());
  zx_status_t ProcessSetFormatResponse(
      const audio_stream_cmd_set_format_resp_t& resp, zx::channel rb_channel)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain_->token());

  // Ring buffer message handlers.
  zx_status_t ProcessGetFifoDepthResponse(
      const audio_rb_cmd_get_fifo_depth_resp_t& resp)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain_->token());
  zx_status_t ProcessGetBufferResponse(
      const audio_rb_cmd_get_buffer_resp_t& resp, zx::vmo rb_vmo)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain_->token());
  zx_status_t ProcessStartResponse(const audio_rb_cmd_start_resp_t& resp)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain_->token());
  zx_status_t ProcessStopResponse(const audio_rb_cmd_stop_resp_t& resp)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain_->token());

  // Transition to the Shutdown state and begin the process of shutting down.
  void ShutdownSelf(const char* debug_reason = nullptr,
                    zx_status_t debug_status = ZX_OK)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain_->token());

  // Evaluate each of our currently pending timeouts and program the command
  // timeout timer appropriately.
  void SetupCommandTimeout()
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain_->token());

  // Update internal plug state bookkeeping and report up to our owner (if
  // enabled)
  void ReportPlugStateChange(bool plugged, zx_time_t plug_time)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain_->token());

  // Simple accessors
  bool operational() const
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain_->token()) {
    return (state_ != State::Uninitialized) && (state_ != State::Shutdown);
  }

  bool fetching_formats() const
      FXL_EXCLUSIVE_LOCKS_REQUIRED(owner_->mix_domain_->token()) {
    return (fetch_formats_timeout_ != ZX_TIME_INFINITE);
  }

  // Accessors for the ring buffer pointer and the current output clock
  // transformation.
  //
  // Note: Only the AudioDriver writes to these, and only when in our owner's
  // mixing execution domain.  It is safe for our owner to read these objects,
  // but only when operating in the mixing domain.  Unfortunately, it is not
  // practical to use the static thread safety annotation to prove that we are
  // accessing these variable from the mixing domain.  Instead, we...
  //
  // 1) Make these methods private.
  // 2) Make the AudioDevice class (our owner) a friend.
  // 3) Expose protected accessors in the AudioDevice class which demand that we
  //    be executing in the mix domain.
  //
  // This should be a strong enough guarantee to warrant disabling the thread
  // safety analysis here.
  const fbl::RefPtr<DriverRingBuffer>& ring_buffer() const
      FXL_NO_THREAD_SAFETY_ANALYSIS {
    return ring_buffer_;
  };

  const TimelineFunction& clock_mono_to_ring_pos_bytes() const
      FXL_NO_THREAD_SAFETY_ANALYSIS {
    return clock_mono_to_ring_pos_bytes_;
  }

  AudioDevice* const owner_;

  State state_ = State::Uninitialized;
  fbl::RefPtr<::dispatcher::Channel> stream_channel_;
  fbl::RefPtr<::dispatcher::Channel> rb_channel_;
  fbl::RefPtr<::dispatcher::Timer> cmd_timeout_;
  zx_time_t last_set_timeout_ = ZX_TIME_INFINITE;

  // State for configured format ranges
  std::vector<audio_stream_format_range_t> format_ranges_;
  zx_time_t fetch_formats_timeout_ = ZX_TIME_INFINITE;

  // Configuration state.
  uint32_t frames_per_sec_;
  uint64_t external_delay_nsec_;
  uint16_t channel_count_;
  audio_sample_format_t sample_format_;
  uint32_t bytes_per_frame_;
  zx_duration_t min_ring_buffer_duration_;
  uint32_t fifo_depth_bytes_;
  uint32_t fifo_depth_frames_;
  zx_time_t configuration_timeout_ = ZX_TIME_INFINITE;

  // A stashed copy of the currently configured format which may be queried by
  // destintions (either outputs or capturers) when determining what mixer to
  // use.
  mutable std::mutex configured_format_lock_;
  AudioMediaTypeDetailsPtr configured_format_
      FXL_GUARDED_BY(configured_format_lock_);

  // Ring buffer state.  Note, the details of the ring buffer state are
  // protected by a lock and changes are tracked with a generation counter.
  // This is importatnt as it allows capturer clients to take a snapshot of the
  // ring buffer state during mixing/resampling operations.
  mutable std::mutex ring_buffer_state_lock_;
  fbl::RefPtr<DriverRingBuffer> ring_buffer_
      FXL_GUARDED_BY(ring_buffer_state_lock_);
  TimelineFunction clock_mono_to_ring_pos_bytes_
      FXL_GUARDED_BY(ring_buffer_state_lock_);
  uint32_t end_fence_to_start_fence_frames_
      FXL_GUARDED_BY(ring_buffer_state_lock_) = 0;
  GenerationId ring_buffer_state_gen_ FXL_GUARDED_BY(ring_buffer_state_lock_);

  // Plug detection state.
  bool pd_enabled_ = false;
  zx_time_t pd_enable_timeout_ = ZX_TIME_INFINITE;

  mutable fbl::Mutex plugged_lock_;
  bool plugged_ FXL_GUARDED_BY(plugged_lock_) = false;
  zx_time_t plug_time_ FXL_GUARDED_BY(plugged_lock_) = 0;
};

}  // namespace audio
}  // namespace media
