// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_HOST_FIDL_PROFILE_SERVER_H_
#define GARNET_DRIVERS_BLUETOOTH_HOST_FIDL_PROFILE_SERVER_H_

#include <fuchsia/bluetooth/bredr/cpp/fidl.h>
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

#include "garnet/drivers/bluetooth/host/fidl/server_base.h"
#include "garnet/drivers/bluetooth/lib/sdp/service_record.h"

namespace bthost {

// Implements the bredr::Profile FIDL interface.
class ProfileServer
    : public AdapterServerBase<fuchsia::bluetooth::bredr::Profile> {
 public:
  ProfileServer(
      fxl::WeakPtr<::btlib::gap::Adapter> adapter,
      fidl::InterfaceRequest<fuchsia::bluetooth::bredr::Profile> request);
  ~ProfileServer() override;

 private:
  // fuchsia::bluetooth::bredr::Profile overrides:
  void AddService(fuchsia::bluetooth::bredr::ServiceDefinition definition,
                  fuchsia::bluetooth::bredr::SecurityLevel sec_level,
                  bool devices, AddServiceCallback callback) override;
  void DisconnectClient(::fidl::StringPtr device_id,
                        uint64_t service_id) override;
  void RemoveService(uint64_t service_id) override;

  // Registered service IDs handed out, correlated with Service Handles.
  std::map<uint64_t, btlib::sdp::ServiceHandle> registered_;

  // Keep this as the last member to make sure that all weak pointers are
  // invalidated before other members get destroyed.
  fxl::WeakPtrFactory<ProfileServer> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ProfileServer);
};

}  // namespace bthost

#endif  // GARNET_DRIVERS_BLUETOOTH_HOST_FIDL_PROFILE_SERVER_H_
