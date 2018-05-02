// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/media/simple_sine/simple_sine.h"
#include "lib/fxl/logging.h"

namespace {
// TODO(mpuryear): Make frame rate, num_chans, payload size & num, frequency,
// amplitude and duration into command line inputs; the below become defaults.

// Set the renderer format to: 48 kHz, mono, 16-bit LPCM (signed integer).
constexpr float kRendererFrameRate = 48000.0f;
constexpr size_t kNumChannels = 1;

// For this example, feed audio to the system in payloads of 10 milliseconds.
constexpr size_t kMSecsPerPayload = 10;
constexpr size_t kFramesPerPayload =
    kMSecsPerPayload * kRendererFrameRate / 1000;
// Contiguous payload buffers mapped into a single 1-sec memory section (id 0)
constexpr size_t kNumPayloads = 100;
// Play a sine wave that is 439 Hz, at 1/8 of full-scale volume.
constexpr float kFrequency = 439.0f;
constexpr float kFrequencyScalar = kFrequency * 2 * M_PI / kRendererFrameRate;
constexpr float kAmplitudeScalar = 0.125f;
// Loop for 2 seconds.
constexpr size_t kTotalDurationSecs = 2;
constexpr size_t kNumPacketsToSend =
    kTotalDurationSecs * kRendererFrameRate / kFramesPerPayload;
}  // namespace

namespace examples {

MediaApp::MediaApp(fxl::Closure quit_callback) : quit_callback_(quit_callback) {
  FXL_DCHECK(quit_callback_);
}

// Prepare for playback, submit initial data and start the presentation timeline
void MediaApp::Run(component::ApplicationContext* app_context) {
  sample_size_ = (use_float_ ? sizeof(float) : sizeof(int16_t));
  payload_size_ = kFramesPerPayload * kNumChannels * sample_size_;
  total_mapping_size_ = payload_size_ * kNumPayloads;

  AcquireRenderer(app_context);
  SetMediaType();

  if (CreateMemoryMapping() != ZX_OK) {
    Shutdown();
    return;
  }

  WriteAudioIntoBuffer();
  for (size_t payload_num = 0; payload_num < kNumPayloads; ++payload_num) {
    SendPacket(CreateAudioPacket(payload_num));
  }

  audio_renderer_->PlayNoReply(media::kNoTimestamp, media::kNoTimestamp);
}

// Use ApplicationContext to acquire AudioServerPtr and AudioRenderer2Ptr in
// turn. Set error handler, in case of channel closure.
void MediaApp::AcquireRenderer(component::ApplicationContext* app_context) {
  // AudioServer is needed only long enough to create the renderer(s).
  media::AudioServerPtr audio_server =
      app_context->ConnectToEnvironmentService<media::AudioServer>();

  // Only one of [AudioRenderer or MediaRenderer] must be kept open for playback
  audio_server->CreateRendererV2(audio_renderer_.NewRequest());

  audio_renderer_.set_error_handler([this]() {
    FXL_LOG(ERROR) << "AudioRenderer connection lost. Quitting.";
    Shutdown();
  });
}

// Set the Mediarenderer's audio format to stereo 48kHz 16-bit (LPCM).
void MediaApp::SetMediaType() {
  FXL_DCHECK(audio_renderer_);

  media::AudioPcmFormat format;

  format.sample_format = (use_float_ ? media::AudioSampleFormat::FLOAT
                                     : media::AudioSampleFormat::SIGNED_16);
  format.channels = kNumChannels;
  format.frames_per_second = kRendererFrameRate;

  audio_renderer_->SetPcmFormat(std::move(format));
}

// Create a single Virtual Memory Object, and map enough memory for our audio
// buffers. Reduce the rights and send the handle over to the AudioRenderer to
// act as our shared buffer.
zx_status_t MediaApp::CreateMemoryMapping() {
  zx::vmo payload_vmo;
  zx_status_t status = payload_buffer_.CreateAndMap(
      total_mapping_size_, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
      nullptr, &payload_vmo, ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "VmoMapper:::CreateAndMap failed - " << status;
    return status;
  }

  audio_renderer_->SetPayloadBuffer(std::move(payload_vmo));

  return ZX_OK;
}

// Write a sine wave into our buffer. We'll continuously loop/resubmit it.
void MediaApp::WriteAudioIntoBuffer() {
  float* float_buffer = reinterpret_cast<float*>(payload_buffer_.start());

  for (size_t frame = 0; frame < kFramesPerPayload * kNumPayloads; ++frame) {
    float float_val = kAmplitudeScalar * sin(frame * kFrequencyScalar);

    for (size_t chan_num = 0; chan_num < kNumChannels; ++chan_num) {
      if (use_float_) {
        float_buffer[frame * kNumChannels + chan_num] = float_val;
      } else {
        int16_t int_val = static_cast<int16_t>(
            round(float_val * std::numeric_limits<int16_t>::max()));
        int16_t* int_buffer = reinterpret_cast<int16_t*>(float_buffer);

        int_buffer[frame * kNumChannels + chan_num] = int_val;
      }
    }
  }
}

// We divided our cross-proc buffer into different zones, called payloads.
// Create a packet corresponding to this particular payload.
media::AudioPacket MediaApp::CreateAudioPacket(size_t payload_num) {
  media::AudioPacket packet;
  packet.payload_offset = (payload_num * payload_size_) % total_mapping_size_;
  packet.payload_size = payload_size_;
  return packet;
}

// Submit a packet, incrementing our count of packets sent. When it returns:
// a. if there are more packets to send, create and send the next packet;
// b. if all expected packets have completed, begin closing down the system.
void MediaApp::SendPacket(media::AudioPacket packet) {
  ++num_packets_sent_;
  audio_renderer_->SendPacket(std::move(packet),
                              [this]() { OnSendPacketComplete(); });
}

void MediaApp::OnSendPacketComplete() {
  ++num_packets_completed_;
  FXL_DCHECK(num_packets_completed_ <= kNumPacketsToSend);

  if (num_packets_sent_ < kNumPacketsToSend) {
    SendPacket(CreateAudioPacket(num_packets_sent_));
  } else if (num_packets_completed_ >= kNumPacketsToSend) {
    Shutdown();
  }
}

// Unmap memory, quit message loop (FIDL interfaces auto-delete upon ~MediaApp).
void MediaApp::Shutdown() {
  payload_buffer_.Unmap();
  quit_callback_();
}

}  // namespace examples
