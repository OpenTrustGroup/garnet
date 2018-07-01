// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/memory_format.h"

#include <inttypes.h>

#include <limits>

#include "garnet/bin/zxdb/client/memory_dump.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

const char kUnknownByte[] = "??";
const char kNonAscii =
    ' ';  // When printing ASCII and the character is not in range.

bool IsPrintableAscii(uint8_t c) { return c >= ' ' && c < 0x7f; }

}  // namespace

// Optimized for simplicity over speed.
std::string FormatMemory(const MemoryDump& dump, uint64_t begin, uint32_t size,
                         const MemoryFormatOptions& opts) {
  std::string out;

  // Compute the last address we'll print. Watch for overflow.
  uint64_t max_addr = std::numeric_limits<uint64_t>::max();
  if (max_addr - size > begin)
    max_addr = begin + size - 1;

  // Max address character width.
  int addr_width = 0;
  if (opts.show_addrs) {
    addr_width =
        static_cast<int>(fxl::StringPrintf("%" PRIX64, max_addr).size());
  }

  uint64_t cur = begin;  // Current address being printed.
  std::string line;      // These string buffers are outside the loop to prevent
                         // reallocation.
  std::string address;
  std::string values;
  std::string ascii;
  bool done = false;
  while (!done) {  // Line loop
    // Compute address at beginning of line.
    if (opts.show_addrs) {
      address = fxl::StringPrintf("%0*" PRIX64, addr_width, cur);
      address.append(":  ");
    }

    values.clear();
    ascii.clear();
    for (int i = 0; i < opts.values_per_line; i++) {  // Value loop.
      // Separator between values.
      if (i > 0) {
        if (!done && opts.separator_every > 0 &&
            (i % opts.separator_every) == 0) {
          values.push_back('-');
        } else {
          values.push_back(' ');
        }
      }

      if (!done) {
        uint8_t byte;
        if (dump.GetByte(cur, &byte)) {
          values += fxl::StringPrintf("%02X", static_cast<int>(byte));
          if (IsPrintableAscii(byte)) {
            ascii.push_back(static_cast<char>(byte));
          } else {
            ascii.push_back(kNonAscii);
          }
        } else {
          values.append(kUnknownByte);
          ascii.push_back(kNonAscii);
        }

        // Carefully only increment address if it won't overflow.
        if (cur == max_addr) {
          done = true;
        } else {
          cur++;
        }
      } else {
        // Line is done, but we still want padding for values.
        values.append("  ");
        ascii.push_back(' ');
      }
    }

    // Append the constructed elements.
    out.append(address);  // Will be empty if not showing.
    out.append(values);
    if (opts.show_ascii) {
      out.append("  |");
      out.append(ascii);
    }
    out.push_back('\n');

    address.clear();
    values.clear();
  }

  return out;
}

}  // namespace zxdb
