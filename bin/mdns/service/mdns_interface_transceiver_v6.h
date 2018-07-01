// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_SERVICE_MDNS_INTERFACE_TRANSCEIVER_V6_H_
#define GARNET_BIN_MDNS_SERVICE_MDNS_INTERFACE_TRANSCEIVER_V6_H_

#include "garnet/bin/mdns/service/mdns_interface_transceiver.h"

namespace mdns {

// Provides V6-specific behavior for abstract MdnsInterfaceTransceiver.
class MdnsInterfaceTransceiverV6 : public MdnsInterfaceTransceiver {
 public:
  virtual ~MdnsInterfaceTransceiverV6() override;

 protected:
  // MdnsInterfaceTransceiver overrides.
  int SetOptionJoinMulticastGroup() override;
  int SetOptionOutboundInterface() override;
  int SetOptionUnicastTtl() override;
  int SetOptionMulticastTtl() override;
  int SetOptionFamilySpecific() override;
  int Bind() override;
  int SendTo(const void* buffer, size_t size,
             const SocketAddress& address) override;

 private:
  MdnsInterfaceTransceiverV6(IpAddress address, const std::string& name,
                             uint32_t index);

  friend class MdnsInterfaceTransceiver;  // For constructor.
};

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_SERVICE_MDNS_INTERFACE_TRANSCEIVER_V6_H_
