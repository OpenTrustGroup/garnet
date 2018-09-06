// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_SIGNAL_GENERATOR_SIGNAL_GENERATOR_H_
#define GARNET_BIN_MEDIA_SIGNAL_GENERATOR_SIGNAL_GENERATOR_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/fzl/vmo-mapper.h>

#include "garnet/lib/media/wav_writer/wav_writer.h"
#include "lib/component/cpp/startup_context.h"

namespace media {
namespace tools {

typedef enum {
  kOutputTypeNoise,
  kOutputTypeSine,
  kOutputTypeSquare,
  kOutputTypeSawtooth,
} OutputSignalType;
// TODO(mpuryear): refactor the signal-generation section to make it easier for
// new generators to be added.

class MediaApp {
 public:
  MediaApp(fit::closure quit_callback);

  void set_num_channels(uint32_t num_channels) { num_channels_ = num_channels; }
  void set_frame_rate(uint32_t frame_rate) { frame_rate_ = frame_rate; }
  void set_int16_format(bool use_int16) { use_int16_ = use_int16; }
  void set_int24_format(bool use_int24) { use_int24_ = use_int24; }

  void set_output_type(OutputSignalType output_type) {
    output_signal_type_ = output_type;
  }
  void set_frequency(double frequency) { frequency_ = frequency; }
  void set_amplitude(float amplitude) { amplitude_ = amplitude; }

  void set_duration(double duration_secs) { duration_secs_ = duration_secs; }
  void set_frames_per_payload(uint32_t frames_per_payload) {
    frames_per_payload_ = frames_per_payload;
  }

  void set_save_to_file(bool save_to_file) { save_to_file_ = save_to_file; }
  void set_save_file_name(std::string file_name) { file_name_ = file_name; }

  void set_stream_gain(float stream_gain_db) {
    stream_gain_db_ = stream_gain_db;
  }
  void set_will_set_system_gain(bool set_system_gain) {
    set_system_gain_ = set_system_gain;
  }
  void set_system_gain(float system_gain) { system_gain_db_ = system_gain; }
  void set_system_mute() { set_system_mute_ = true; }
  void set_system_unmute() { set_system_unmute_ = true; }

  void set_will_set_audio_policy(bool set_policy) { set_policy_ = set_policy; }
  void set_audio_policy(fuchsia::media::AudioOutputRoutingPolicy policy) {
    audio_policy_ = policy;
  }

  void Run(component::StartupContext* app_context);

 private:
  bool ParameterRangeChecks();
  void SetupPayloadCoefficients();
  void DisplayConfigurationSettings();
  void AcquireAudioOut(component::StartupContext* app_context);
  void SetStreamType();

  zx_status_t CreateMemoryMapping();

  fuchsia::media::StreamPacket CreateAudioPacket(uint64_t packet_num);
  void GenerateAudioForPacket(fuchsia::media::StreamPacket packet,
                              uint64_t payload_num);
  template <typename SampleType>
  static void WriteAudioIntoBuffer(SampleType* audio_buffer,
                                   uint32_t num_frames,
                                   uint64_t frames_since_start,
                                   OutputSignalType signal_type,
                                   uint32_t num_chans, double frames_per_period,
                                   double amp_scalar);

  void SendPacket(uint64_t payload_num);
  void OnSendPacketComplete();

  void Shutdown();

  fit::closure quit_callback_;

  fuchsia::media::AudioOutPtr audio_out_;
  fuchsia::media::GainControlPtr gain_control_;

  fzl::VmoMapper payload_buffer_;

  uint32_t num_channels_;
  uint32_t frame_rate_;
  bool use_int16_ = false;
  bool use_int24_ = false;
  uint32_t sample_size_;
  uint32_t frame_size_;

  OutputSignalType output_signal_type_;

  double frequency_;
  double frames_per_period_;  // frame_rate_ / frequency_

  double amplitude_;         // Amplitude between 0.0 and 1.0 (full-scale).
  double amplitude_scalar_;  // Amp translated to container-specific magn.

  double duration_secs_;
  uint32_t frames_per_payload_;

  uint32_t total_mapping_size_;
  uint32_t payload_size_;
  uint32_t payloads_per_total_mapping_;

  uint64_t total_frames_to_send_;
  uint64_t num_packets_to_send_;
  uint64_t num_packets_sent_ = 0u;
  uint64_t num_packets_completed_ = 0u;

  bool save_to_file_ = false;
  std::string file_name_;
  media::audio::WavWriter<> wav_writer_;
  bool wav_writer_is_initialized_ = false;

  float stream_gain_db_;
  bool set_system_gain_ = false;
  float system_gain_db_;
  bool set_system_mute_ = false;
  bool set_system_unmute_ = false;

  bool set_policy_ = false;
  fuchsia::media::AudioOutputRoutingPolicy audio_policy_;
};

}  // namespace tools
}  // namespace media

#endif  // GARNET_BIN_MEDIA_SIGNAL_GENERATOR_SIGNAL_GENERATOR_H_
