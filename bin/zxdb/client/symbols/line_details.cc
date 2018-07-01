// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/line_details.h"

#include <ostream>

namespace zxdb {

LineDetails::LineDetails() = default;
LineDetails::LineDetails(FileLine fl) : file_line_(std::move(fl)) {}
LineDetails::~LineDetails() = default;

void LineDetails::Dump(std::ostream& out) const {
  out << file_line_.file() << ":" << file_line_.line() << " ranges = [\n";
  for (const auto& entry : entries_) {
    out << "  " << std::hex << entry.range.begin() << " -> "
        << entry.range.end() << " col = " << entry.column << "\n";
  }
  out << "]\n";
}

}  // namespace zxdb
