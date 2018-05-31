// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include "gtest/gtest.h"
#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/files/unique_fd.h"

namespace files {
namespace {

TEST(Directory, CreateDirectory) {
  std::string cwd = GetCurrentDirectory();

  ScopedTempDir dir;
  EXPECT_TRUE(IsDirectory(dir.path()));
  EXPECT_EQ(0, chdir(dir.path().c_str()));

  EXPECT_TRUE(CreateDirectory("foo/bar"));
  EXPECT_TRUE(IsDirectory("foo"));
  EXPECT_TRUE(IsDirectory("foo/bar"));
  EXPECT_FALSE(IsDirectory("foo/bar/baz"));

  EXPECT_TRUE(CreateDirectory("foo/bar/baz"));
  EXPECT_TRUE(IsDirectory("foo/bar/baz"));

  EXPECT_TRUE(CreateDirectory("qux"));
  EXPECT_TRUE(IsDirectory("qux"));

  EXPECT_EQ(0, chdir(cwd.c_str()));

  std::string abs_path = dir.path() + "/another/one";
  EXPECT_TRUE(CreateDirectory(abs_path));
  EXPECT_TRUE(IsDirectory(abs_path));
}

TEST(Directory, CreateDirectoryAt) {
  std::string cwd = GetCurrentDirectory();

  ScopedTempDir dir;
  EXPECT_TRUE(IsDirectory(dir.path()));
  fxl::UniqueFD root(open(dir.path().c_str(), O_RDONLY));
  EXPECT_TRUE(root.is_valid());
  EXPECT_FALSE(IsDirectoryAt(root.get(), "foo/bar/baz"));
  EXPECT_TRUE(CreateDirectoryAt(root.get(), "foo/bar/baz"));
  EXPECT_TRUE(IsDirectoryAt(root.get(), "foo/bar/baz"));
  EXPECT_TRUE(IsDirectory(dir.path() + "/foo/bar/baz"));
}

}  // namespace
}  // namespace files
