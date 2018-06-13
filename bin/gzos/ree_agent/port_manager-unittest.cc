// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/svc/cpp/service_provider_bridge.h>
#include <ree_agent/cpp/fidl.h>
#include <zircon/compiler.h>

#include "garnet/bin/gzos/ree_agent/port_manager.h"
#include "garnet/bin/gzos/ree_agent/ree_agent.h"
#include "gtest/gtest.h"
#include "lib/ree_agent/cpp/port.h"

namespace ree_agent {

TEST(TipcPortManagerTest, PublishPort) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  component::ServiceProviderBridge service_provider;

  // Create and register TipcPortManager service
  TipcPortManagerImpl port_manager;
  service_provider.AddService<TipcPortManager>(
      [&port_manager](fidl::InterfaceRequest<TipcPortManager> request) {
        port_manager.Bind(fbl::move(request));
      });

  // Grab service directory, this is used by Trusted Application
  // for getting services published by ree_agent
  zx::channel svc_dir = service_provider.OpenAsDirectory();
  ASSERT_TRUE(svc_dir != ZX_HANDLE_INVALID);

  // Create a Services object for Trusted Application to connect
  // TipcPortManager service
  component::Services services;
  services.Bind(fbl::move(svc_dir));

  // Now publish a test port at Trusted Application
  fbl::String port_path("org.opentrust.test");
  TipcPortImpl test_port(&services, port_path,
                         [](fidl::InterfaceRequest<TipcChannel> channel) {
                           // Connection Request callback, leave it empty
                         });
  test_port.Publish([](Status status) { EXPECT_EQ(status, Status::OK); });

  loop.RunUntilIdle();

  // Publish a duplicated path should fail
  TipcPortImpl duplicated_port(&services, port_path,
                               [](fidl::InterfaceRequest<TipcChannel> channel) {
                                 // Connection Request callback, leave it empty
                               });
  duplicated_port.Publish(
      [](Status status) { EXPECT_EQ(status, Status::ALREADY_EXISTS); });

  loop.RunUntilIdle();

  // Verify the published port in port table
  TipcPortTableEntry* entry;
  ASSERT_EQ(port_manager.Find(port_path, entry), ZX_OK);
  ASSERT_TRUE(entry != nullptr);
  EXPECT_STREQ(entry->path().c_str(), port_path.c_str());
}

}  // namespace ree_agent
