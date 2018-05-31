// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MEASURE_DURATION_H_
#define GARNET_LIB_MEASURE_DURATION_H_

#include <map>
#include <stack>
#include <unordered_map>
#include <vector>

#include <trace-reader/reader.h>

#include "garnet/lib/measure/event_spec.h"
#include "lib/fxl/macros.h"

namespace tracing {
namespace measure {

// A "duration" measurement specifies a single trace event. The target event can
// be recorded as a "duration" or as an "async" event.
struct DurationSpec {
  uint64_t id;
  EventSpec event;
};

class MeasureDuration {
 public:
  explicit MeasureDuration(std::vector<DurationSpec> specs);

  // Processes a recorded trace event. Returns true on success and false if the
  // record was ignored due to an error in the provided data. Trace events must
  // be processed in non-decreasing order of timestamps.
  bool Process(const trace::Record::Event& event);

  // Returns the results of the measurements. The results are represented as a
  // map of measurement ids to lists of time deltas representing the durations
  // of the matching trace events.
  const std::unordered_map<uint64_t, std::vector<uint64_t>>& results() {
    return results_;
  }

 private:
  // Async and flow event ids are scoped to event names. To match "end" events
  // with "begin" events, we keep a map of unmatched begin events.
  struct PendingBeginKey {
    enum class Type { Async, Flow };

    Type type;
    fbl::String category;
    fbl::String name;
    uint64_t id;

    bool operator<(const PendingBeginKey& other) const;
  };

  PendingBeginKey MakeKey(const trace::Record::Event& event);

  bool ProcessAsyncOrFlowBegin(const trace::Record::Event& event);
  bool ProcessAsyncOrFlowEnd(const trace::Record::Event& event);
  bool ProcessDurationBegin(const trace::Record::Event& event);
  bool ProcessDurationEnd(const trace::Record::Event& event);

  void AddResult(uint64_t spec_id, trace_ticks_t from, trace_ticks_t to);

  std::vector<DurationSpec> specs_;
  std::unordered_map<uint64_t, std::vector<uint64_t>> results_;

  std::map<PendingBeginKey, trace_ticks_t> pending_begins_;

  // Duration events recorded on a thread can be nested. To match "end" events
  // with "begin" events, we keep a per-thread stack of timestamps of unmatched
  // "begin" events.
  std::map<trace::ProcessThread, std::stack<trace_ticks_t>> duration_stacks_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MeasureDuration);
};

}  // namespace measure
}  // namespace tracing

#endif  // GARNET_LIB_MEASURE_DURATION_H_
