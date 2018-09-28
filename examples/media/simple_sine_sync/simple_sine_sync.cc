// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/media/simple_sine_sync/simple_sine_sync.h"

#include <fuchsia/media/cpp/fidl.h>
#include <math.h>
#include <zircon/syscalls.h>

#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fxl/logging.h"

namespace {
// Set the audio stream_type to: 44.1 kHz, stereo, 16-bit LPCM (signed integer).
constexpr double kFrameRate = 44100.0;
constexpr size_t kNumChannels = 2;

// For this example, feed audio to the system in payloads of 10 milliseconds.
constexpr size_t kMSecsPerPayload = 10;
constexpr size_t kFramesPerPayload = kMSecsPerPayload * kFrameRate / 1000;
constexpr size_t kTotalMappingFrames = kFrameRate;
constexpr size_t kNumPayloads = kTotalMappingFrames / kFramesPerPayload;

// Play a sine wave that is 439 Hz, at 1/8 of full-scale volume.
constexpr double kFrequency = 439.0;
constexpr double kAmplitudeScalar = 0.125;
constexpr double kFrequencyScalar = 2 * M_PI * kFrequency / kFrameRate;

// Loop for 2 seconds.
constexpr size_t kTotalDurationSecs = 2;
constexpr size_t kNumPacketsToSend =
    kTotalDurationSecs * kFrameRate / kFramesPerPayload;

}  // namespace

namespace examples {

MediaApp::MediaApp(std::unique_ptr<component::StartupContext> context)
    : context_(std::move(context)) {}
MediaApp::~MediaApp() {}

// Prepare for playback, compute playback data, supply media packets, start.
int MediaApp::Run() {
  sample_size_ = use_float_ ? sizeof(float) : sizeof(int16_t);
  payload_size_ = kFramesPerPayload * kNumChannels * sample_size_;
  total_mapping_size_ = kTotalMappingFrames * kNumChannels * sample_size_;

  if (high_water_mark_ < low_water_mark_) {
    high_water_mark_ = low_water_mark_;
  }
  if (verbose_) {
    printf("Low water mark: %ldms\n", low_water_mark_ / 1000000);
    printf("High water mark: %ldms\n", high_water_mark_ / 1000000);
  }

  if (!AcquireAudioRendererSync()) {
    FXL_LOG(ERROR) << "Could not acquire AudioRendererSync";
    return 1;
  }

  if (!SetStreamType()) {
    return 2;
  }

  if (CreateMemoryMapping() != ZX_OK) {
    return 3;
  }

  WriteAudioIntoBuffer(payload_buffer_.start(), kTotalMappingFrames);

  // Query the current absolute minimum lead time demanded by the mixer, then
  // adjust our high and low water marks to stand off by that much as well.
  //
  // Note: Since we are using timing to drive this entire example (and not
  // the occasional asynchronous callback), to be perfectly correct, we would
  // want to dynamically adjust our lead time in response to changing
  // conditions.  Sadly, there is really no good way to do this with a purely
  // single threaded synchronous interface.
  int64_t min_lead_time;
  audio_renderer_sync_->GetMinLeadTime(&min_lead_time);
  low_water_mark_ += min_lead_time;
  high_water_mark_ += min_lead_time;

  if ((min_lead_time > 0) && verbose_) {
    printf("Adjusted high and low water marks by min lead time %.3lfms\n",
           min_lead_time / 1000000.0);

    printf("Low water mark: %ldms\n", low_water_mark_ / 1000000);
    printf("High water mark: %ldms\n", high_water_mark_ / 1000000);
  }

  constexpr zx_duration_t nsec_per_payload = ZX_MSEC(kMSecsPerPayload);
  uint32_t initial_payloads = std::min<uint32_t>(
      (high_water_mark_ + nsec_per_payload - 1) / nsec_per_payload,
      kNumPacketsToSend);

  while (num_packets_sent_ < initial_payloads) {
    SendAudioPacket(CreateAudioPacket(num_packets_sent_));
  }

  int64_t ref_start_time;
  int64_t media_start_time;
  // Begin playback now, using default values for input params reference_time
  // and media_time. As out params, we return the actual reference and media
  // times that were used. In effect, by using NO_TIMESTAMP for these two input
  // values, we align the following two things: "a local time of _As Soon As
  // We Safely Can_" and "the audio that I gave a PTS of _Zero_."
  audio_renderer_sync_->Play(fuchsia::media::NO_TIMESTAMP,
                             fuchsia::media::NO_TIMESTAMP, &ref_start_time,
                             &media_start_time);
  start_time_known_ = true;

  // TODO(johngro): This program is making the assumption that the platform's
  // default reference clock is CLOCK_MONOTONIC.  While that is (currently)
  // true, it will not always be so.  When we start to introduce the posibility
  // that the default audio reference clock is different, we need to come back
  // and either...
  //
  // 1) Explicitly set our reference clock to CLOCK_MONO (causing
  //    micro-resampling in the mixer to effect clock correction, if needed)
  // -- OR --
  // 2) Obtain a handle to the system's default reference clock and use that to
  //    control timing, instead of blindly using CLOCK_MONO.
  FXL_DCHECK(ref_start_time >= 0);
  FXL_DCHECK(media_start_time == 0);
  clock_start_time_ = static_cast<zx_time_t>(ref_start_time);

  while (num_packets_sent_ < kNumPacketsToSend) {
    WaitForPackets(num_packets_sent_);
    RefillBuffer();
  }

  WaitForPackets(kNumPacketsToSend);  // Wait for the last packet to complete.

  return 0;
}

// Connect (synchronously) to the Audio service and get an AudioRendererSync.
bool MediaApp::AcquireAudioRendererSync() {
  fuchsia::media::AudioSyncPtr audio;

  context_->ConnectToEnvironmentService(audio.NewRequest());
  return audio->CreateAudioRenderer(audio_renderer_sync_.NewRequest()) == ZX_OK;
}

// Set the AudioRendererSync's audio stream_type to stereo 48kHz.
bool MediaApp::SetStreamType() {
  FXL_DCHECK(audio_renderer_sync_);

  fuchsia::media::AudioStreamType stream_type;
  stream_type.sample_format =
      use_float_ ? fuchsia::media::AudioSampleFormat::FLOAT
                 : fuchsia::media::AudioSampleFormat::SIGNED_16;
  stream_type.channels = kNumChannels;
  stream_type.frames_per_second = kFrameRate;

  if (audio_renderer_sync_->SetPcmStreamType(std::move(stream_type)) != ZX_OK) {
    FXL_LOG(ERROR) << "Could not set stream type";
    return false;
  }
  return true;
}

// Create a single Virtual Memory Object, and map enough memory for our audio
// buffers. Open a PacketConsumer, and send it a duplicate handle of our VMO.
zx_status_t MediaApp::CreateMemoryMapping() {
  zx::vmo payload_vmo;
  zx_status_t status = payload_buffer_.CreateAndMap(
      total_mapping_size_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr,
      &payload_vmo, ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "VmoMapper:::CreateAndMap failed - " << status;
    return status;
  }

  // We map a single payload buffer; each packet references a region within it.
  audio_renderer_sync_->AddPayloadBuffer(0, std::move(payload_vmo));

  return ZX_OK;
}

// Write a sine wave into our audio buffer. We'll continuously loop/resubmit it.
void MediaApp::WriteAudioIntoBuffer(void* buffer, size_t num_frames) {
  for (size_t frame = 0; frame < num_frames; ++frame) {
    float val =
        kAmplitudeScalar * sin(static_cast<double>(frame) * kFrequencyScalar);

    for (size_t chan_num = 0; chan_num < kNumChannels; ++chan_num) {
      if (use_float_) {
        float* float_buffer = reinterpret_cast<float*>(buffer);
        float_buffer[frame * kNumChannels + chan_num] = val;
      } else {
        int16_t* int_buffer = reinterpret_cast<int16_t*>(buffer);
        int_buffer[frame * kNumChannels + chan_num] = static_cast<int16_t>(
            round(val * std::numeric_limits<int16_t>::max()));
      }
    }
  }
}

// Create a packet for this payload.
// By not specifying a presentation timestamp for each packet (we allow the
// default value of fuchsia::media::NO_TIMESTAMP), we rely on the
// AudioRendererSync to treat the sequence of packets as a contiguous unbroken
// stream of audio. We just need to make sure we present packets early enough.
fuchsia::media::StreamPacket MediaApp::CreateAudioPacket(size_t payload_num) {
  fuchsia::media::StreamPacket packet;

  // leave packet.pts as the default (fuchsia::media::NO_TIMESTAMP)
  // leave packet.payload_buffer_id as default (0): we only map a single buffer

  packet.payload_offset = (payload_num % kNumPayloads) * payload_size_;
  packet.payload_size = payload_size_;
  return packet;
}

// Submit a packet, incrementing our count of packets sent.
bool MediaApp::SendAudioPacket(fuchsia::media::StreamPacket packet) {
  if (verbose_) {
    const float delay =
        (start_time_known_
             ? (float)zx_clock_get(ZX_CLOCK_MONOTONIC) - clock_start_time_
             : 0) /
        1000000;
    printf("SendAudioPacket num %zu time %.2f\n", num_packets_sent_, delay);
  }

  ++num_packets_sent_;

  // Note: SendPacketNoReply returns immediately, before packet is consumed.
  return audio_renderer_sync_->SendPacketNoReply(std::move(packet)) == ZX_OK;
}

// Stay ahead of the presentation timeline, by the amount high_water_mark_.
// We must wait until a packet is consumed before reusing its buffer space.
// For more fine-grained awareness/control of buffers, clients should use the
// (asynchronous) AudioRenderer interface and process callbacks from SendPacket.
bool MediaApp::RefillBuffer() {
  const zx_time_t now = zx_clock_get(ZX_CLOCK_MONOTONIC);
  const zx_duration_t time_data_needed =
      now - std::min(now, clock_start_time_) + high_water_mark_;
  size_t num_payloads_needed =
      ceil(static_cast<float>(time_data_needed) / ZX_MSEC(kMSecsPerPayload));
  num_payloads_needed = std::min(kNumPacketsToSend, num_payloads_needed);

  if (verbose_) {
    printf("RefillBuffer  now: %.3f start: %.3f :: need %lu (%.4f), sent %lu\n",
           (float)now / 1000000, (float)clock_start_time_ / 1000000,
           num_payloads_needed * kMSecsPerPayload,
           (float)time_data_needed / 1000000,
           num_packets_sent_ * kMSecsPerPayload);
  }
  while (num_packets_sent_ < num_payloads_needed) {
    if (!SendAudioPacket(CreateAudioPacket(num_packets_sent_))) {
      return false;
    }
  }

  return true;
}

void MediaApp::WaitForPackets(size_t num_packets) {
  const zx_duration_t audio_submitted =
      ZX_MSEC(kMSecsPerPayload) * num_packets_sent_;

  FXL_DCHECK(num_packets_sent_ <= kNumPacketsToSend);
  zx_time_t wake_time = clock_start_time_ + audio_submitted;
  if (num_packets_sent_ < kNumPacketsToSend) {
    wake_time -= low_water_mark_;
  }

  const zx_time_t now = zx_clock_get(ZX_CLOCK_MONOTONIC);
  if (wake_time > now) {
    if (verbose_) {
      const zx_duration_t nap_duration = wake_time - now;
      printf("sleeping for %.05f ms\n", (double)nap_duration / 1000000);
    }
    zx_nanosleep(wake_time);
  }
}

}  // namespace examples
