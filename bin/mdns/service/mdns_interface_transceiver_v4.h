// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/mdns/service/mdns_interface_transceiver.h"

namespace mdns {

// Provides V4-specific behavior for abstract MdnsInterfaceTransceiver.
class MdnsInterfaceTransceiverV4 : public MdnsInterfaceTransceiver {
 public:
  virtual ~MdnsInterfaceTransceiverV4() override;

 protected:
  // MdnsInterfaceTransceiver overrides.
  int SetOptionJoinMulticastGroup() override;
  int SetOptionOutboundInterface() override;
  int SetOptionUnicastTtl() override;
  int SetOptionMulticastTtl() override;
  int SetOptionFamilySpecific() override;
  int Bind() override;
  int SendTo(const void* buffer,
             size_t size,
             const SocketAddress& address) override;

 private:
  MdnsInterfaceTransceiverV4(IpAddress address,
                             const std::string& name,
                             uint32_t index);

  friend class MdnsInterfaceTransceiver;  // For constructor.
};

}  // namespace mdns
