// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_TESTS_INTEGRATION_TESTS_H_
#define GARNET_BIN_TRACE_TESTS_INTEGRATION_TESTS_H_

#include <stddef.h>
#include <string>

#include <lib/async/dispatcher.h>

#include "garnet/bin/trace/spec.h"

using TestRunner = bool (const tracing::Spec& spec,
                         async_dispatcher_t* dispatcher);
using TestVerifier = bool (const tracing::Spec& spec,
                           const std::string& test_output_file);

struct IntegrationTest {
  const char* name;
  TestRunner* run;
  TestVerifier* verify;
};

extern const IntegrationTest kFillBufferIntegrationTest;
extern const IntegrationTest kSimpleIntegrationTest;

// When emitting a small fixed number of events, emit this amount.
// We don't need many, and certainly not so much that we overflow the buffer:
// Here we can verify we got precisely the number of events we expected.
constexpr size_t kNumSimpleTestEvents = 10;

// When waiting for tracing to start, wait this long.
constexpr zx::duration kStartTimeout{zx::sec(10)};

// Emit |num_iterations| records that |VerifyTestEvents()| knows how to test.
extern void WriteTestEvents(size_t num_records);

// Verify a trace generated with |WriteTestRecords()|.
// Returns a boolean indicating success.
// On success returns the number of events found in |*out_num_events|.
extern bool VerifyTestEvents(const std::string& test_output_file,
                             size_t* out_num_events);

// Write as many records as we can to ensure a buffer of size
// |buffer_size_in_mb| is full, and fill it |num_times|.
extern void FillBuffer(size_t num_times, size_t buffer_size_in_mb);

// Verify the trace generated by |FillBuffer()|.
// Returns a boolean indicating success.
extern bool VerifyFullBuffer(const std::string& test_output_file,
                             tracing::BufferingMode buffering_mode,
                             size_t buffer_size_in_mb);

// Wait for tracing to start or |timeout|.
// Returns true if tracing has started.
bool WaitForTracingToStart(zx::duration timeout);

#endif  // GARNET_BIN_TRACE_TESTS_INTEGRATION_TESTS_H_
