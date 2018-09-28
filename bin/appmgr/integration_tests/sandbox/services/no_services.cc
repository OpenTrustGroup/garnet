// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/integration_tests/sandbox/namespace_test.h"

#include <string>
#include <vector>

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/testing/appmgr/cpp/fidl.h>
#include <zircon/errors.h>
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/fxl/files/directory.h"

TEST_F(NamespaceTest, NoServices) {
  fuchsia::testing::appmgr::TestServiceSyncPtr test_service;
  fuchsia::testing::appmgr::TestService2SyncPtr test_service2;
  ConnectToService(test_service.NewRequest());
  ConnectToService(test_service2.NewRequest());
  RunLoopUntilIdle();

  ::fidl::StringPtr message, message2;
  ASSERT_EQ(ZX_ERR_PEER_CLOSED, test_service->GetMessage(&message));
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, test_service2->GetMessage(&message2));

  // readdir should list services in sandbox.
  std::vector<std::string> files;
  ASSERT_TRUE(files::ReadDirContents("/svc", &files));
  EXPECT_THAT(files, ::testing::UnorderedElementsAre("."));
}
