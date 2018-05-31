// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/audio_server/standard_output_base.h"

namespace media {
namespace audio {

class ThrottleOutput : public StandardOutputBase {
 public:
  static fbl::RefPtr<AudioOutput> Create(AudioDeviceManager* manager);
  ~ThrottleOutput() override;

 protected:
  explicit ThrottleOutput(AudioDeviceManager* manager);

  // AudioOutput Implementation
  void OnWakeup() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token()) override;

  // StandardOutputBase Implementation
  bool StartMixJob(MixJob* job, fxl::TimePoint process_start) override;
  bool FinishMixJob(const MixJob& job) override;

 private:
  fxl::TimePoint last_sched_time_;
  bool uninitialized_ = true;
};

}  // namespace audio
}  // namespace media
