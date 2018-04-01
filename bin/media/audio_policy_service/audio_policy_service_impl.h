// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "garnet/bin/media/util/fidl_publisher.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include <fuchsia/cpp/media.h>
#include <fuchsia/cpp/media.h>
#include <fuchsia/cpp/media.h>

namespace media {

class AudioPolicyServiceImpl : public AudioPolicyService {
 public:
  AudioPolicyServiceImpl(
      std::unique_ptr<component::ApplicationContext> application_context);
  ~AudioPolicyServiceImpl() override;

  // AudioPolicyService implementation.
  void GetStatus(uint64_t version_last_seen,
                 GetStatusCallback callback) override;

  void SetSystemAudioGain(float db) override;

  void SetSystemAudioMute(bool muted) override;

 private:
  static constexpr float kDefaultSystemAudioGainDb = -12.0f;
  static constexpr bool kDefaultSystemMuted = false;

  // Loads the status file and initializes the audio service.
  void InitializeAudioService();

  // Returns a new status struct built from |system_audio_gain_db_| and
  // |system_audio_muted_|.
  AudioPolicyStatus Status();

  // Attempts to load the status file, updating |system_audio_gain_db_| and
  // |system_audio_muted_| if successful.
  void LoadStatus();

  // Saves the status to the status file.
  void SaveStatus();

  // Updates the audio service with the current master gain based on
  // |system_audio_gain_db_| and |system_audio_muted_|.
  void UpdateAudioService();

  // Ensures that |audio_service_| is bound.
  void EnsureAudioService();

  // Returns the effective system audio gain based on |system_audio_gain_db_|
  // and |system_audio_muted_|.
  float effective_system_audio_gain() {
    return system_audio_muted_ ? kMutedGain
                               : system_audio_gain_db_;
  }

  std::unique_ptr<component::ApplicationContext> application_context_;
  fidl::BindingSet<AudioPolicyService> bindings_;
  float system_audio_gain_db_ = kDefaultSystemAudioGainDb;
  bool system_audio_muted_ = kDefaultSystemMuted;
  FidlPublisher<GetStatusCallback> status_publisher_;
  AudioServerPtr audio_service_;
  uint32_t initialize_attempts_remaining_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AudioPolicyServiceImpl);
};

}  // namespace media
