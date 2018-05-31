// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <dispatcher-pool/dispatcher-channel.h>
#include <dispatcher-pool/dispatcher-timer.h>
#include <dispatcher-pool/dispatcher-wakeup-event.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/slab_allocator.h>
#include <fbl/unique_ptr.h>

#include <media/cpp/fidl.h>
#include "garnet/bin/media/audio_server/audio_object.h"
#include "garnet/bin/media/audio_server/mixer/mixer.h"
#include "garnet/bin/media/audio_server/mixer/output_formatter.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/media/timeline/timeline_function.h"
#include "lib/media/timeline/timeline_rate.h"

namespace media {
namespace audio {

class AudioServerImpl;

class AudioCapturerImpl : public AudioObject, public AudioCapturer {
 public:
  static fbl::RefPtr<AudioCapturerImpl> Create(
      fidl::InterfaceRequest<AudioCapturer> audio_capturer_request,
      AudioServerImpl* owner, bool loopback);

  bool loopback() const { return loopback_; }
  void SetInitialFormat(AudioMediaTypeDetails format)
      FXL_LOCKS_EXCLUDED(mix_domain_->token());
  void Shutdown() FXL_LOCKS_EXCLUDED(mix_domain_->token());

 protected:
  friend class fbl::RefPtr<AudioCapturerImpl>;
  ~AudioCapturerImpl() override;
  zx_status_t InitializeSourceLink(const AudioLinkPtr& link) override;

 private:
  // Notes about the AudioCapturerImpl state machine.
  //
  // :: WaitingForVmo ::
  // Audio capturers start in this mode.  They should have a default capture
  // mode set, and will accept a mode change up until the point where they have
  // a shared payload VMO assigned to them.  At this point they transition into
  // the OperatingSync state.  Only the main server thread may transition out of
  // this state.
  //
  // :: OperatingSync ::
  // After a mode has been assigned and a shared payload VMO has provided, the
  // capturer is now operating in synchronous mode.  Clients may provided
  // buffers to be filled using the CaptureAt method and may cancel these
  // buffers using the Flush method.  They may also transition to asynchronous
  // mode by calling StartAsyncCapture, but only when there are no pending
  // buffers in flight.  Only the main server thread may transition out of
  // this state.
  //
  // :: OperatingAsync ::
  // Capturers enter OperatingAsync after a successful call to
  // StartAsyncCapturer.  Threads from the mix_domain allocate and fill pending
  // payload buffers, then signal the main server thread in order to send them
  // back to the client over the AudioCapturerClient interface provided when
  // starting.  CaptureAt and Flush are illegal operations while in this state.
  // clients may begin the process of returning to synchronous capture mode by
  // calling StopAsyncCapture.  Only the main server thread may transition out
  // of this state.
  //
  // :: AsyncStopping ::
  // Capturers enter AsyncStopping after a successful call to
  // StopAsyncCapturer.  A thread from the mix_domain will handle the details
  // of stopping, including transferring all partially filled pending buffers to
  // the finished queue.  Aside from setting the gain, all operations are
  // illegal while the capturer is in the process of stopping.  Once the mix
  // domain thread has finished cleaning up, it will transition to the
  // AsyncStoppingCallbackPending state and signal the main server thread in
  // order to complete the process.  Only a mix domain thread may transition out
  // of this state.
  //
  // :: AsyncStoppingCallbackPending ::
  // Capturers enter AsyncStoppingCallbackPending after a mix domain thread has
  // finished the process of shutting down the capture process and is ready to
  // signal to the client that the capturer is now in synchronous capture mode
  // again.  The main server thread will send all partially and completely
  // filled buffers to the user, ensuring that there is at least one buffer sent
  // indicating end-of-stream, even if that buffer needs to be of zero length.
  // Finally, the main server thread will signal that the stopping process is
  // finished using the client supplied callback (if any), and finally
  // transition back to the OperatingSync state.
  enum class State {
    WaitingForVmo,
    OperatingSync,
    OperatingAsync,
    AsyncStopping,
    AsyncStoppingCallbackPending,
    Shutdown,
  };

  struct PendingCaptureBuffer;

  using PcbAllocatorTraits =
      fbl::StaticSlabAllocatorTraits<fbl::unique_ptr<PendingCaptureBuffer>>;
  using PcbAllocator = fbl::SlabAllocator<PcbAllocatorTraits>;
  using PcbList = fbl::DoublyLinkedList<fbl::unique_ptr<PendingCaptureBuffer>>;

  struct PendingCaptureBuffer : public fbl::SlabAllocated<PcbAllocatorTraits>,
                                public fbl::DoublyLinkedListable<
                                    fbl::unique_ptr<PendingCaptureBuffer>> {
    PendingCaptureBuffer(uint32_t of, uint32_t nf, const CaptureAtCallback c)
        : offset_frames(of), num_frames(nf), cbk(std::move(c)) {}

    static AtomicGenerationId sequence_generator;

    const uint32_t offset_frames;
    const uint32_t num_frames;
    const CaptureAtCallback cbk;

    int64_t capture_timestamp = kNoTimestamp;
    uint32_t flags = 0;
    uint32_t filled_frames = 0;
    const uint32_t sequence_number = sequence_generator.Next();
  };

  friend PcbAllocator;

  // TODO(mpuryear): per MTWN-129, combine this with RendererBookkeeping, and
  // integrate it into the Mixer class itself.
  struct CaptureLinkBookkeeping : public AudioLink::Bookkeeping {
    std::unique_ptr<Mixer> mixer;
    TimelineFunction dest_frames_to_frac_source_frames;
    TimelineFunction clock_mono_to_src_frames_fence;
    uint32_t step_size;
    uint32_t modulo;
    uint32_t denominator() const {
      return dest_frames_to_frac_source_frames.rate().reference_delta();
    }
    uint32_t dest_trans_gen_id = kInvalidGenerationId;
    uint32_t source_trans_gen_id = kInvalidGenerationId;
  };

  AudioCapturerImpl(
      fidl::InterfaceRequest<AudioCapturer> audio_capturer_request,
      AudioServerImpl* owner, bool loopback);

  // AudioCapturer FIDL implementation
  void GetMediaType(GetMediaTypeCallback cbk) final;
  void SetMediaType(MediaType media_type) final;
  void SetGain(float gain) final;
  void SetPayloadBuffer(zx::vmo payload_buf_vmo) final;
  void CaptureAt(uint32_t offset_frames, uint32_t num_frames,
                 CaptureAtCallback cbk) final;
  void Flush() final;
  void FlushWithCallback(FlushWithCallbackCallback cbk) final;
  void StartAsyncCapture(uint32_t frames_per_packet) final;
  void StopAsyncCapture() final;
  void StopAsyncCaptureWithCallback(
      StopAsyncCaptureWithCallbackCallback cbk) final;

  // Methods used by the capture/mixer thread(s).  Must be called from the
  // mix_domain.
  zx_status_t Process() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  bool MixToIntermediate(uint32_t mix_frames)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  void UpdateTransformation(CaptureLinkBookkeeping* bk,
                            const AudioDriver::RingBufferSnapshot& rb_snap)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  void DoStopAsyncCapture() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());
  bool QueueNextAsyncPendingBuffer()
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token())
          FXL_LOCKS_EXCLUDED(pending_lock_);
  void ShutdownFromMixDomain()
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  // Thunk to send finished buffers back to the user, and to finish an async
  // mode stop operation.
  void FinishAsyncStopThunk() FXL_LOCKS_EXCLUDED(mix_domain_->token());
  void FinishBuffersThunk() FXL_LOCKS_EXCLUDED(mix_domain_->token());

  // Helper function used to return a set of pending capture buffers to a user.
  void FinishBuffers(const PcbList& finished_buffers)
      FXL_LOCKS_EXCLUDED(mix_domain_->token());

  // Bookkeeping helper.
  void UpdateFormat(media::AudioSampleFormat sample_format, uint32_t channels,
                    uint32_t frames_per_second)
      FXL_LOCKS_EXCLUDED(mix_domain_->token());

  // Select a mixer for the link supplied and return true, or return false if
  // one cannot be found.
  zx_status_t ChooseMixer(const std::shared_ptr<AudioLink>& link);

  fidl::Binding<AudioCapturer> binding_;
  AudioServerImpl* owner_ = nullptr;
  std::atomic<State> state_;
  const bool loopback_;

  // Capture format and gain state.
  AudioMediaTypeDetailsPtr format_;
  uint32_t bytes_per_frame_;
  TimelineRate frames_to_clock_mono_rate_;
  uint32_t max_frames_per_capture_;
  std::atomic<float> db_gain_;

  // Shared buffer state
  zx::vmo payload_buf_vmo_;
  void* payload_buf_virt_ = nullptr;
  uint64_t payload_buf_size_ = 0;
  uint32_t payload_buf_frames_ = 0;

  // Execution domain/dispatcher stuff for mixing.
  fbl::RefPtr<::dispatcher::ExecutionDomain> mix_domain_;
  fbl::RefPtr<::dispatcher::WakeupEvent> mix_wakeup_;
  fbl::RefPtr<::dispatcher::Timer> mix_timer_;

  // Queues of capture buffers supplied by the client and waiting to be filled,
  // or waiting to be returned.
  fbl::Mutex pending_lock_;
  PcbList pending_capture_buffers_ FXL_GUARDED_BY(pending_lock_);
  PcbList finished_capture_buffers_ FXL_GUARDED_BY(pending_lock_);

  // Intermediate mixing buffer and output formatter
  std::unique_ptr<OutputFormatter> output_formatter_;
  std::unique_ptr<int32_t[]> mix_buf_;

  // Vector used to hold references to our source links while we are mixing
  // (instead of holding the lock which prevents source_links_ mutation for the
  // entire mix job)
  std::vector<std::shared_ptr<AudioLink>> source_link_refs_
      FXL_GUARDED_BY(mix_domain_->token());

  // Capture bookkeeping
  bool async_mode_ = false;
  TimelineFunction frames_to_clock_mono_ FXL_GUARDED_BY(mix_domain_->token());
  GenerationId frames_to_clock_mono_gen_ FXL_GUARDED_BY(mix_domain_->token());
  int64_t frame_count_ FXL_GUARDED_BY(mix_domain_->token()) = 0;

  uint32_t async_frames_per_packet_;
  uint32_t async_next_frame_offset_ FXL_GUARDED_BY(mix_domain_->token()) = 0;
  StopAsyncCaptureWithCallbackCallback pending_async_stop_cbk_;

  fbl::Mutex sources_lock_;
  std::set<std::shared_ptr<AudioLink>,
           std::owner_less<std::shared_ptr<AudioLink>>>
      sources_ FXL_GUARDED_BY(sources_lock_);
};

}  // namespace audio
}  // namespace media

FWD_DECL_STATIC_SLAB_ALLOCATOR(
    ::media::audio::AudioCapturerImpl::PcbAllocatorTraits);
