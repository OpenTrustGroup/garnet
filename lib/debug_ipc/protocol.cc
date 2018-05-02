// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/protocol.h"

namespace debug_ipc {

constexpr uint32_t MsgHeader::kSerializedHeaderSize;

constexpr uint64_t HelloReply::kStreamSignature;
constexpr uint32_t HelloReply::kCurrentVersion;

}  // namespace debug_ipc
