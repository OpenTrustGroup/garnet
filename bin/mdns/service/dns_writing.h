// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_SERVICE_DNS_WRITING_H_
#define GARNET_BIN_MDNS_SERVICE_DNS_WRITING_H_

#include <memory>

#include "garnet/bin/mdns/service/dns_message.h"
#include "garnet/bin/mdns/service/packet_writer.h"

namespace mdns {

template <typename T>
PacketWriter& operator<<(PacketWriter& writer,
                         const std::shared_ptr<T>& value) {
  return writer << *value;
}

PacketWriter& operator<<(PacketWriter& writer, const DnsName& value);
PacketWriter& operator<<(PacketWriter& writer, const DnsV4Address& value);
PacketWriter& operator<<(PacketWriter& writer, const DnsV6Address& value);
PacketWriter& operator<<(PacketWriter& writer, const DnsType& value);
PacketWriter& operator<<(PacketWriter& writer, const DnsClass& value);
PacketWriter& operator<<(PacketWriter& writer, const DnsClassAndFlag& value);
PacketWriter& operator<<(PacketWriter& writer, const DnsHeader& value);
PacketWriter& operator<<(PacketWriter& writer, const DnsQuestion& value);
PacketWriter& operator<<(PacketWriter& writer, const DnsResource& value);
PacketWriter& operator<<(PacketWriter& writer, const DnsResourceDataA& value);
PacketWriter& operator<<(PacketWriter& writer, const DnsResourceDataNs& value);
PacketWriter& operator<<(PacketWriter& writer,
                         const DnsResourceDataCName& value);
PacketWriter& operator<<(PacketWriter& writer, const DnsResourceDataPtr& value);
PacketWriter& operator<<(PacketWriter& writer, const DnsResourceDataTxt& value);
PacketWriter& operator<<(PacketWriter& writer,
                         const DnsResourceDataAaaa& value);
PacketWriter& operator<<(PacketWriter& writer, const DnsResourceDataSrv& value);
PacketWriter& operator<<(PacketWriter& writer, const DnsResourceDataOpt& value);
PacketWriter& operator<<(PacketWriter& writer,
                         const DnsResourceDataNSec& value);
PacketWriter& operator<<(PacketWriter& writer, const DnsMessage& value);

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_SERVICE_DNS_WRITING_H_
