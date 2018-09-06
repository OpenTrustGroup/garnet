// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_output.h"

#include <stdint.h>
#include <memory>

CodecOutput::CodecOutput(
    uint64_t stream_lifetime_ordinal,
    std::shared_ptr<const fuchsia::mediacodec::CodecOutputConfig> config,
    std::unique_ptr<const fuchsia::mediacodec::CodecPacket> packet,
    bool end_of_stream)
    : stream_lifetime_ordinal_(stream_lifetime_ordinal),
      config_(config),
      packet_(std::move(packet)),
      end_of_stream_(end_of_stream) {
  // nothing else to do here
}
