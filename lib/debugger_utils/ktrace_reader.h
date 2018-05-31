// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Simple reader for ktrace files.
// TODO: IWBN if there was a libktrace to replace this, but that can
// probably wait until ktrace is combined into ftrace

#pragma once

#include <stdint.h>

#include <zircon/ktrace.h>

namespace debugserver {
namespace ktrace {

union KtraceRecord {
  ktrace_header_t hdr;
  ktrace_rec_32b_t r_16B;
  ktrace_rec_32b_t r_32B;
  ktrace_rec_name_t r_NAME;
  uint8_t raw[256];
};

// The type of the function to pass to ReadFile.
typedef int RecordReader(KtraceRecord* rec, void* arg);

// Read all of |fd|, calling |reader| for each record found.
// If |reader| returns zero reading continues. Otherwise the result of
// |reader| is an error code, and is returned as the result of ReadFile().

int ReadFile(int fd, RecordReader* reader, void* arg);

}  // namespace ktrace
}  // namespace debugserver
