// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_SERVICE_MDNS_FIDL_UTIL_H_
#define GARNET_BIN_MDNS_SERVICE_MDNS_FIDL_UTIL_H_

#include <fuchsia/mdns/cpp/fidl.h>

#include <fuchsia/mdns/cpp/fidl.h>
#include "garnet/bin/mdns/service/mdns.h"
#include "garnet/bin/mdns/service/socket_address.h"

namespace mdns {

// mDNS utility functions relating to fidl types.
class MdnsFidlUtil {
 public:
  static const std::string kFuchsiaServiceName;

  static fuchsia::mdns::MdnsServiceInstancePtr CreateServiceInstance(
      const std::string& service_name, const std::string& instance_name,
      const SocketAddress& v4_address, const SocketAddress& v6_address,
      const std::vector<std::string>& text);

  static void UpdateServiceInstance(
      const fuchsia::mdns::MdnsServiceInstancePtr& service_instance,
      const SocketAddress& v4_address, const SocketAddress& v6_address,
      const std::vector<std::string>& text);

  static fuchsia::netstack::SocketAddressPtr CreateSocketAddressIPv4(
      const IpAddress& ip_address);

  static fuchsia::netstack::SocketAddressPtr CreateSocketAddressIPv6(
      const IpAddress& ip_address);

  static fuchsia::netstack::SocketAddressPtr CreateSocketAddressIPv4(
      const SocketAddress& socket_address);

  static fuchsia::netstack::SocketAddressPtr CreateSocketAddressIPv6(
      const SocketAddress& socket_address);

  static IpAddress IpAddressFrom(const fuchsia::netstack::NetAddress* addr);

  static std::unique_ptr<Mdns::Publication> Convert(
      const fuchsia::mdns::MdnsPublicationPtr& publication_ptr);
};

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_SERVICE_MDNS_FIDL_UTIL_H_
