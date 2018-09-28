// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/command_utils.h"

#include <inttypes.h>

#include <limits>

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/symbols/function.h"
#include "garnet/bin/zxdb/symbols/location.h"
#include "gtest/gtest.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

TEST(CommandUtils, StringToInt) {
  // Leading 0's not octal.
  int result = 0;
  EXPECT_FALSE(StringToInt("010", &result).has_error());
  EXPECT_EQ(10, result);

  // Negative hexadecimal.
  EXPECT_FALSE(StringToInt("-0x1a", &result).has_error());
  EXPECT_EQ(-0x1a, result);

  // Test at the limits.
  constexpr int kMax = std::numeric_limits<int>::max();
  EXPECT_FALSE(StringToInt(fxl::StringPrintf("%d", kMax), &result).has_error());
  EXPECT_EQ(kMax, result);

  constexpr int kMin = std::numeric_limits<int>::lowest();
  EXPECT_FALSE(StringToInt(fxl::StringPrintf("%d", kMin), &result).has_error());
  EXPECT_EQ(kMin, result);

  // Test just beyond the limits.
  int64_t kBeyondMax = static_cast<int64_t>(kMax) + 1;
  EXPECT_TRUE(StringToInt(fxl::StringPrintf("%" PRId64, kBeyondMax), &result)
                  .has_error());

  int64_t kBeyondMin = static_cast<int64_t>(kMin) - 1;
  EXPECT_TRUE(StringToInt(fxl::StringPrintf("%" PRId64, kBeyondMin), &result)
                  .has_error());
}

TEST(CommandUtils, StringToUint32) {
  uint32_t result = 0;
  EXPECT_FALSE(StringToUint32("032", &result).has_error());
  EXPECT_EQ(32u, result);

  // Test at and just beyond the limits.
  EXPECT_FALSE(StringToUint32("0xffffffff", &result).has_error());
  EXPECT_EQ(0xffffffff, result);
  EXPECT_TRUE(StringToUint32("0x100000000", &result).has_error());
}

TEST(CommandUtils, StringToUint64) {
  uint64_t result = 0;
  EXPECT_FALSE(StringToUint64("1234", &result).has_error());
  EXPECT_EQ(1234u, result);

  // Empty string.
  EXPECT_TRUE(StringToUint64("", &result).has_error());

  // Non-numbers.
  EXPECT_TRUE(StringToUint64("asdf", &result).has_error());
  EXPECT_TRUE(StringToUint64(" ", &result).has_error());

  // We don't allow "+" for positive numbers.
  EXPECT_TRUE(StringToUint64("+1234", &result).has_error());
  EXPECT_EQ(0u, result);

  // No leading spaces permitted.
  EXPECT_TRUE(StringToUint64(" 1234", &result).has_error());

  // No trailing spaces permitted.
  EXPECT_TRUE(StringToUint64("1234 ", &result).has_error());

  // Leading 0's should still be decimal, don't trigger octal.
  EXPECT_FALSE(StringToUint64("01234", &result).has_error());
  EXPECT_EQ(1234u, result);

  // Hex digits invalid without proper prefix.
  EXPECT_TRUE(StringToUint64("12a34", &result).has_error());

  // Valid hex number
  EXPECT_FALSE(StringToUint64("0x1A2a34", &result).has_error());
  EXPECT_EQ(0x1a2a34u, result);

  // Valid hex number with capital X prefix at the max of a 64-bit int.
  EXPECT_FALSE(StringToUint64("0XffffFFFFffffFFFF", &result).has_error());
  EXPECT_EQ(0xffffFFFFffffFFFFu, result);
}

TEST(CommandUtils, ReadUint64Arg) {
  Command cmd;
  uint64_t out;

  Err err = ReadUint64Arg(cmd, 0, "code", &out);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Not enough arguments when reading the code.", err.msg());

  std::vector<std::string> args;
  args.push_back("12");
  args.push_back("0x67");
  args.push_back("notanumber");
  cmd.set_args(std::move(args));

  err = ReadUint64Arg(cmd, 0, "code", &out);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(12u, out);

  err = ReadUint64Arg(cmd, 1, "code", &out);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(0x67u, out);

  err = ReadUint64Arg(cmd, 2, "code", &out);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Invalid number \"notanumber\" when reading the code.", err.msg());
}

TEST(CommandUtils, ParseHostPort) {
  std::string host;
  uint16_t port;

  // Host good.
  EXPECT_FALSE(ParseHostPort("google.com:1234", &host, &port).has_error());
  EXPECT_EQ("google.com", host);
  EXPECT_EQ(1234, port);

  EXPECT_FALSE(ParseHostPort("google.com", "1234", &host, &port).has_error());
  EXPECT_EQ("google.com", host);
  EXPECT_EQ(1234, port);

  // IPv4 Good.
  EXPECT_FALSE(ParseHostPort("192.168.0.1:1234", &host, &port).has_error());
  EXPECT_EQ("192.168.0.1", host);
  EXPECT_EQ(1234, port);

  EXPECT_FALSE(ParseHostPort("192.168.0.1", "1234", &host, &port).has_error());
  EXPECT_EQ("192.168.0.1", host);
  EXPECT_EQ(1234, port);

  // IPv6 Good.
  EXPECT_FALSE(ParseHostPort("[1234::5678]:1234", &host, &port).has_error());
  EXPECT_EQ("1234::5678", host);
  EXPECT_EQ(1234, port);

  EXPECT_FALSE(ParseHostPort("[1234::5678]", "1234", &host, &port).has_error());
  EXPECT_EQ("1234::5678", host);
  EXPECT_EQ(1234, port);

  EXPECT_FALSE(ParseHostPort("1234::5678", "1234", &host, &port).has_error());
  EXPECT_EQ("1234::5678", host);
  EXPECT_EQ(1234, port);

  // Missing ports.
  EXPECT_TRUE(ParseHostPort("google.com", &host, &port).has_error());
  EXPECT_TRUE(ParseHostPort("192.168.0.1", &host, &port).has_error());
  EXPECT_TRUE(ParseHostPort("1234::5678", &host, &port).has_error());
  EXPECT_TRUE(ParseHostPort("[1234::5678]", &host, &port).has_error());

  // Bad port values.
  EXPECT_TRUE(ParseHostPort("google.com:0", &host, &port).has_error());
  EXPECT_TRUE(ParseHostPort("google.com:99999999", &host, &port).has_error());
}

TEST(CommandUtils, DescribeLocation) {
  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  EXPECT_EQ("<invalid address>", DescribeLocation(Location(), true));

  // Address-only location.
  EXPECT_EQ(
      "0x12345",
      DescribeLocation(Location(Location::State::kAddress, 0x12345), false));

  // Function-only location.
  fxl::RefPtr<Function> function(fxl::MakeRefCounted<Function>());
  function->set_assigned_name("Func");
  function->set_code_ranges({{0x1200, 0x1300}});
  EXPECT_EQ("Func() + 0x34 (no line info)",
            DescribeLocation(Location(0x1234, FileLine(), 0, symbol_context,
                                      LazySymbol(function)),
                             false));

  // Same as above but location is before the function address (probably
  // something is corrupt). It should omit the offset.
  EXPECT_EQ("Func()",
            DescribeLocation(Location(0x1100, FileLine(), 0, symbol_context,
                                      LazySymbol(function)),
                             false));

  // File/line-only location.
  EXPECT_EQ("foo.cc:21",
            DescribeLocation(Location(0x1234, FileLine("/path/foo.cc", 21), 0,
                                      symbol_context),
                             false));

  // Full location.
  Location loc(0x1234, FileLine("/path/foo.cc", 21), 0, symbol_context,
               LazySymbol(function));
  EXPECT_EQ("0x1234, Func() • foo.cc:21", DescribeLocation(loc, true));
  EXPECT_EQ("Func() • foo.cc:21", DescribeLocation(loc, false));
}

TEST(CommandUtils, DescribeFileLine) {
  FileLine fl("/path/to/foo.cc", 21);
  EXPECT_EQ("/path/to/foo.cc:21", DescribeFileLine(fl, true));
  EXPECT_EQ("foo.cc:21", DescribeFileLine(fl, false));

  // Missing line number.
  EXPECT_EQ("foo.cc:?", DescribeFileLine(FileLine("/path/foo.cc", 0), false));

  // Missing both.
  EXPECT_EQ("?:?", DescribeFileLine(FileLine(), false));
}

}  // namespace zxdb
