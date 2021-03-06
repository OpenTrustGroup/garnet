// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_RESULTS_OUTPUT_H_
#define GARNET_BIN_TRACE_RESULTS_OUTPUT_H_

#include <ostream>
#include <vector>

#include "garnet/bin/trace/spec.h"
#include "garnet/lib/measure/results.h"

namespace tracing {

void OutputResults(std::ostream& out,
                   const std::vector<measure::Result>& results);

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_RESULTS_OUTPUT_H_
