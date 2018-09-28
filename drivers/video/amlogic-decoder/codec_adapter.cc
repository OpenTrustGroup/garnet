// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_adapter.h"

#include <zircon/assert.h>

CodecAdapter::CodecAdapter(std::mutex& lock,
                           CodecAdapterEvents* codec_adapter_events)
    : lock_(lock), events_(codec_adapter_events) {
  ZX_DEBUG_ASSERT(events_);
  // nothing else to do here
}

CodecAdapter::~CodecAdapter() {
  // nothing to do here
}
