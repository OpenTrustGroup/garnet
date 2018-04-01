// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>
#include <iostream>
#include <limits>

#include "garnet/examples/media/tones/tones.h"

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include <fuchsia/cpp/media.h>

// TODO(dalesat): Remove once the mixer supports floats.
#define FLOAT_SAMPLES_SUPPORTED 0

namespace examples {
namespace {


static constexpr uint32_t kChannelCount = 1;
static constexpr uint32_t kFramesPerSecond  = 48000;
static constexpr uint32_t kFramesPerBuffer  = 480;
static constexpr uint32_t kTargetPayloadsInFlight = 2;
static constexpr float kEffectivelySilentVolume = 0.001f;
static constexpr float kNoteZeroFrequency = 110.0f;
static constexpr float kVolume = 0.2f;
static constexpr float kDecay = 0.95f;
static constexpr uint32_t kBeatsPerMinute = 90;

// Translates a note number into a frequency.
float Note(int32_t note) {
  return kNoteZeroFrequency * pow(2.0f, note / 12.0f);
}

// Translates a beat number into a time.
constexpr int64_t Beat(float beat) {
  return static_cast<int64_t>((beat * 60.0f * kFramesPerSecond) /
                              kBeatsPerMinute);
}

#if !FLOAT_SAMPLES_SUPPORTED

// Converts float samples to signed 16 samples, cheap and dirty.
void ConvertFloatToSigned16(float* source, int16_t* dest, size_t sample_count) {
  FXL_DCHECK(source);
  FXL_DCHECK(dest);

  for (size_t i = 0; i < sample_count; ++i) {
    float sample = *source;
    if (sample > 1.0f) {
      *dest = std::numeric_limits<int16_t>::max();
    } else if (sample < -1.0f) {
      *dest = std::numeric_limits<int16_t>::min();
    } else {
      *dest = static_cast<int16_t>(sample * 0x7fff);
    }

    ++source;
    ++dest;
  }
}

static constexpr media::AudioSampleFormat kSampleFormat =
  media::AudioSampleFormat::SIGNED_16;
static constexpr uint32_t kBytesPerFrame = kChannelCount * sizeof(uint16_t);
#else
static constexpr media::AudioSampleFormat kSampleFormat =
  media::AudioSampleFormat::FLOAT;
static constexpr uint32_t kBytesPerFrame = kChannelCount * sizeof(float);
#endif

static constexpr size_t kBytesPerBuffer = kBytesPerFrame * kFramesPerBuffer;
static constexpr size_t kTotalMappingSize =
  kBytesPerBuffer * kTargetPayloadsInFlight;

static const std::map<int, float> notes_by_key_ = {
    {'a', Note(-4)}, {'z', Note(-3)}, {'s', Note(-2)}, {'x', Note(-1)},
    {'c', Note(0)},  {'f', Note(1)},  {'v', Note(2)},  {'g', Note(3)},
    {'b', Note(4)},  {'n', Note(5)},  {'j', Note(6)},  {'m', Note(7)},
    {'k', Note(8)},  {',', Note(9)},  {'l', Note(10)}, {'.', Note(11)},
    {'/', Note(12)}, {'\'', Note(13)}};

}  // namespace

Tones::Tones(bool interactive) : interactive_(interactive) {
  // Allocate our shared payload buffer and pass a handle to it over to the
  // renderer.
  zx::vmo payload_vmo;
  zx_status_t status = payload_buffer_.CreateAndMap(
      kTotalMappingSize,
      ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
      nullptr,
      &payload_vmo,
      ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);

  if (status != ZX_OK) {
    std::cerr << "VmoMapper:::CreateAndMap failed - " << status;
    return;
  }

  // Connect to the audio service and get a renderer.
  auto application_context =
    component::ApplicationContext::CreateFromStartupInfo();

  media::AudioServerPtr audio_server =
      application_context->ConnectToEnvironmentService<media::AudioServer>();

  audio_server->CreateRendererV2(audio_renderer_.NewRequest());

  audio_renderer_.set_error_handler([this]() {
    std::cerr << "Unexpected error: channel to audio service closed\n";
    Quit();
  });

  // Configure the format of the renderer.
  media::AudioPcmFormat format;
  format.sample_format = kSampleFormat;
  format.channels = kChannelCount;
  format.frames_per_second = kFramesPerSecond;
  audio_renderer_->SetPcmFormat(std::move(format));

  // Assign our shared payload buffer to the renderer.
  audio_renderer_->SetPayloadBuffer(std::move(payload_vmo));

  // Configure the renderer to use input frames of audio as its PTS units.
  audio_renderer_->SetPtsUnits(kFramesPerSecond, 1);

  // Configure the renderer to use input frames for the presentation timestamp
  // units instead of defaulting to nanoseconds.

  if (interactive_) {
    std::cout << "| | | |  |  | | | |  |  | | | | | |  |  | |\n";
    std::cout << "|A| |S|  |  |F| |G|  |  |J| |K| |L|  |  |'|\n";
    std::cout << "+-+ +-+  |  +-+ +-+  |  +-+ +-+ +-+  |  +-+\n";
    std::cout << " |   |   |   |   |   |   |   |   |   |   | \n";
    std::cout << " | Z | X | C | V | B | N | M | , | . | / | \n";
    std::cout << "-+---+---+---+---+---+---+---+---+---+---+-\n";
  } else {
    std::cout << "Playing a tune. Use '--interactive' to play the keyboard.\n";
    BuildScore();
  }

  // Post a task to be called when we need to |Send|.
  auto& task_runner = fsl::MessageLoop::GetCurrent()->task_runner();
  task_runner->PostTask([this]() { Start(); });

  WaitForKeystroke();
}

Tones::~Tones() {}

void Tones::Quit() {
  audio_renderer_.Unbind();
  fsl::MessageLoop::GetCurrent()->PostQuitTask();
}

void Tones::WaitForKeystroke() {
  fd_waiter_.Wait(
      [this](zx_status_t status, uint32_t events) { HandleKeystroke(); }, 0,
      POLLIN);
}

void Tones::HandleKeystroke() {
  int c = std::tolower(getc(stdin));

  auto iter = notes_by_key_.find(c);
  if (iter != notes_by_key_.end()) {
    tone_generators_.emplace_back(kFramesPerSecond, iter->second, kVolume,
                                  kDecay);
  }

  switch (c) {
    case 'q':
    case 0x1b:  // escape
      Quit();
      return;
    default:
      break;
  }

  WaitForKeystroke();
}

void Tones::BuildScore() {
  frequencies_by_pts_.emplace(Beat(0.0f), Note(12));
  frequencies_by_pts_.emplace(Beat(1.0f), Note(11));
  frequencies_by_pts_.emplace(Beat(2.0f), Note(9));
  frequencies_by_pts_.emplace(Beat(3.0f), Note(7));
  frequencies_by_pts_.emplace(Beat(4.0f), Note(5));
  frequencies_by_pts_.emplace(Beat(5.0f), Note(4));
  frequencies_by_pts_.emplace(Beat(6.0f), Note(2));
  frequencies_by_pts_.emplace(Beat(7.0f), Note(7));
  frequencies_by_pts_.emplace(Beat(8.0f), Note(9));
  frequencies_by_pts_.emplace(Beat(9.0f), Note(4));
  frequencies_by_pts_.emplace(Beat(10.0f), Note(5));
  frequencies_by_pts_.emplace(Beat(11.0f), Note(0));
  frequencies_by_pts_.emplace(Beat(12.0f), Note(2));
  frequencies_by_pts_.emplace(Beat(13.0f), Note(7));
  frequencies_by_pts_.emplace(Beat(14.0f), Note(0));
  frequencies_by_pts_.emplace(Beat(14.0f), Note(4));
  frequencies_by_pts_.emplace(Beat(14.0f), Note(7));
}

void Tones::Start() {
  Send(kTargetPayloadsInFlight);
  audio_renderer_->PlayNoReply(media::kNoTimestamp,
                               media::kNoTimestamp);
}

void Tones::Send(uint32_t amt) {
  while (!done() && amt--) {
    // Allocate packet and locate its position in the buffer.
    media::AudioPacket packet;
    packet.payload_offset = (pts_ * kBytesPerFrame) % payload_buffer_.size();
    packet.payload_size = kBytesPerBuffer;

    FXL_DCHECK((packet.payload_offset + packet.payload_size) <=
                payload_buffer_.size());

    auto payload_ptr = reinterpret_cast<uint8_t*>(payload_buffer_.start())
      + packet.payload_offset;

    // Fill it with audio.
#if FLOAT_SAMPLES_SUPPORTED
    FillBuffer(reinterpret_cast<float*>(payload_ptr));
#else
    float buffer[kFramesPerBuffer * kChannelCount];
    FillBuffer(buffer);
    ConvertFloatToSigned16(buffer,
                           reinterpret_cast<int16_t*>(payload_ptr),
                           kFramesPerBuffer * kChannelCount);
#endif

    // Send it.
    if (!done()) {
      audio_renderer_->SendPacket(std::move(packet), [this] { Send(1); });
    } else {
      audio_renderer_->SendPacket(std::move(packet), [this] { Quit(); });
    }
  }
}

void Tones::FillBuffer(float* buffer) {
  // Zero out the buffer, because the tone generators mix into it.
  std::memset(buffer, 0, kFramesPerBuffer * 4);

  // Mix in the tone generators we've already created.
  for (auto iter = tone_generators_.begin(); iter != tone_generators_.end();) {
    if (iter->volume() <= kEffectivelySilentVolume) {
      iter = tone_generators_.erase(iter);
    } else {
      iter->MixSamples(buffer, kFramesPerBuffer, kChannelCount);
      ++iter;
    }
  }

  // Create new tone generators as needed.
  while (!frequencies_by_pts_.empty()) {
    int64_t when = frequencies_by_pts_.begin()->first;
    float frequency = frequencies_by_pts_.begin()->second;

    if (when >= pts_ + kFramesPerBuffer) {
      break;
    }

    frequencies_by_pts_.erase(frequencies_by_pts_.begin());

    int64_t offset = when - pts_;
    tone_generators_.emplace_back(kFramesPerSecond, frequency, kVolume, kDecay);

    // Mix in the new tone generator starting at the correct offset in the
    // buffer.
    tone_generators_.back().MixSamples(buffer + (offset * kChannelCount),
                                       kFramesPerBuffer - offset,
                                       kChannelCount);
  }

  pts_ += kFramesPerBuffer;
}

}  // namespace examples
