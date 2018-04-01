// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/vmo_mapper.h>

#include "lib/app/cpp/application_context.h"
#include <fuchsia/cpp/media.h>
#include <fuchsia/cpp/media.h>

namespace examples {

class MediaApp {
 public:
  MediaApp();
  ~MediaApp();

  void Run(component::ApplicationContext* app_context);

 private:
  void AcquireRenderer(component::ApplicationContext* app_context);
  void SetMediaType();

  zx_status_t CreateMemoryMapping();
  void WriteAudioIntoBuffer();

  media::AudioPacket CreateAudioPacket(size_t packet_num);
  void SendPacket(media::AudioPacket packet);
  void OnSendPacketComplete();

  void Shutdown();

  media::AudioRenderer2Ptr audio_renderer_;

  fbl::VmoMapper payload_buffer_;
  size_t num_packets_sent_ = 0u;
  size_t num_packets_completed_ = 0u;
};

}  // namespace examples
