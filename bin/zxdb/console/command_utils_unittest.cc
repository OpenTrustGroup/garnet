// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/command_utils.h"

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "gtest/gtest.h"

namespace zxdb {

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
}  // namespace zxdb
