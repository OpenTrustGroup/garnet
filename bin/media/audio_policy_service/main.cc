// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_policy_service/audio_policy_service_impl.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/message_loop.h"

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;

  media::AudioPolicyServiceImpl impl(
      component::ApplicationContext::CreateFromStartupInfo());

  loop.Run();
  return 0;
}
