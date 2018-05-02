// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>
#include <gflags/gflags.h>
#include <perftest/perftest.h>

#include "channels.h"
#include "round_trips.h"

// Command line arguments used internally for launching subprocesses.
DEFINE_uint32(channel_read, 0, "Launch a process to read from a channel");
DEFINE_uint32(channel_write, 0, "Launch a process to write to a channel");
DEFINE_string(subprocess, "", "Launch a process to run the named function");

namespace fbenchmark {

int BenchmarksMain(int argc, char** argv, bool run_gbenchmark) {
  // Check for arguments that are used by test cases for launching
  // subprocesses.  We check for them before calling gflags because gflags
  // gives an error for any options it does not recognize, such as those
  // accepted by perftest::PerfTestMain().
  if (argc >= 2 &&
      (strcmp(argv[1], "--channel_read") == 0 ||
       strcmp(argv[1], "--channel_write") == 0 ||
       strcmp(argv[1], "--subprocess") == 0)) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    if (FLAGS_channel_read > 0) {
      return channel_read(FLAGS_channel_read);
    } else if (FLAGS_channel_write > 0) {
      return channel_write(FLAGS_channel_write);
    } else if (FLAGS_subprocess != "") {
      RunSubprocess(FLAGS_subprocess.c_str());
      return 0;
    }
  }

  if (argc >= 2 && strcmp(argv[1], "-p") == 0) {
    return perftest::PerfTestMain(argc, argv);
  }
  if (run_gbenchmark) {
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    return 0;
  }
  return perftest::PerfTestMain(argc, argv);
}

}  // namespace fbenchmark
