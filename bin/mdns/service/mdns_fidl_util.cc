// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mdns/service/mdns_fidl_util.h"

#include "lib/fsl/types/type_converters.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/type_converter.h"

namespace mdns {

// static
const std::string MdnsFidlUtil::kFuchsiaServiceName = "_fuchsia._tcp.";

// static
fuchsia::mdns::MdnsServiceInstancePtr MdnsFidlUtil::CreateServiceInstance(
    const std::string& service_name, const std::string& instance_name,
    const SocketAddress& v4_address, const SocketAddress& v6_address,
    const std::vector<std::string>& text) {
  fuchsia::mdns::MdnsServiceInstancePtr service_instance =
      fuchsia::mdns::MdnsServiceInstance::New();

  service_instance->service_name = service_name;
  service_instance->instance_name = instance_name;
  service_instance->text = fxl::To<fidl::VectorPtr<fidl::StringPtr>>(text);

  if (v4_address.is_valid()) {
    service_instance->v4_address = CreateSocketAddressIPv4(v4_address);
  }

  if (v6_address.is_valid()) {
    service_instance->v6_address = CreateSocketAddressIPv6(v6_address);
  }

  return service_instance;
}

// static
void MdnsFidlUtil::UpdateServiceInstance(
    const fuchsia::mdns::MdnsServiceInstancePtr& service_instance,
    const SocketAddress& v4_address, const SocketAddress& v6_address,
    const std::vector<std::string>& text) {
  service_instance->text = fxl::To<fidl::VectorPtr<fidl::StringPtr>>(text);

  if (v4_address.is_valid()) {
    service_instance->v4_address = CreateSocketAddressIPv4(v4_address);
  } else {
    service_instance->v4_address.reset();
  }

  if (v6_address.is_valid()) {
    service_instance->v6_address = CreateSocketAddressIPv6(v6_address);
  } else {
    service_instance->v6_address.reset();
  }
}

// static
fuchsia::netstack::SocketAddressPtr MdnsFidlUtil::CreateSocketAddressIPv4(
    const IpAddress& ip_address) {
  if (!ip_address) {
    return nullptr;
  }

  FXL_DCHECK(ip_address.is_v4());

  fuchsia::netstack::SocketAddressPtr result =
      fuchsia::netstack::SocketAddress::New();
  result->addr.family = fuchsia::netstack::NetAddressFamily::IPV4;
  result->addr.ipv4 = fuchsia::netstack::Ipv4Address::New();

  FXL_DCHECK(result->addr.ipv4->addr.count() == ip_address.byte_count());
  std::memcpy(result->addr.ipv4->addr.mutable_data(), ip_address.as_bytes(),
              result->addr.ipv4->addr.count());

  return result;
}

// static
fuchsia::netstack::SocketAddressPtr MdnsFidlUtil::CreateSocketAddressIPv6(
    const IpAddress& ip_address) {
  if (!ip_address) {
    return nullptr;
  }

  FXL_DCHECK(ip_address.is_v6());

  fuchsia::netstack::SocketAddressPtr result =
      fuchsia::netstack::SocketAddress::New();
  result->addr.family = fuchsia::netstack::NetAddressFamily::IPV6;
  result->addr.ipv6 = fuchsia::netstack::Ipv6Address::New();

  FXL_DCHECK(result->addr.ipv6->addr.count() == ip_address.byte_count());
  std::memcpy(result->addr.ipv6->addr.mutable_data(), ip_address.as_bytes(),
              result->addr.ipv6->addr.count());

  return result;
}

// static
fuchsia::netstack::SocketAddressPtr MdnsFidlUtil::CreateSocketAddressIPv4(
    const SocketAddress& socket_address) {
  if (!socket_address) {
    return nullptr;
  }

  FXL_DCHECK(socket_address.is_v4());

  fuchsia::netstack::SocketAddressPtr result =
      CreateSocketAddressIPv4(socket_address.address());

  result->port = socket_address.port().as_uint16_t();

  return result;
}

// static
fuchsia::netstack::SocketAddressPtr MdnsFidlUtil::CreateSocketAddressIPv6(
    const SocketAddress& socket_address) {
  if (!socket_address) {
    return nullptr;
  }

  FXL_DCHECK(socket_address.is_v6());

  fuchsia::netstack::SocketAddressPtr result =
      CreateSocketAddressIPv6(socket_address.address());

  result->port = socket_address.port().as_uint16_t();

  return result;
}

// static
IpAddress MdnsFidlUtil::IpAddressFrom(
    const fuchsia::netstack::NetAddress* addr) {
  FXL_DCHECK(addr != nullptr);
  switch (addr->family) {
    case fuchsia::netstack::NetAddressFamily::IPV4:
      if (!addr->ipv4) {
        return IpAddress();
      }

      FXL_DCHECK(addr->ipv4->addr.count() == sizeof(in_addr));
      return IpAddress(
          *reinterpret_cast<const in_addr*>(addr->ipv4->addr.data()));
    case fuchsia::netstack::NetAddressFamily::IPV6:
      if (!addr->ipv6) {
        return IpAddress();
      }

      FXL_DCHECK(addr->ipv6->addr.count() == sizeof(in6_addr));
      return IpAddress(
          *reinterpret_cast<const in6_addr*>(addr->ipv6->addr.data()));
    default:
      return IpAddress();
  }
}

// static
std::unique_ptr<Mdns::Publication> MdnsFidlUtil::Convert(
    const fuchsia::mdns::MdnsPublicationPtr& publication_ptr) {
  if (!publication_ptr) {
    return nullptr;
  }

  auto publication = Mdns::Publication::Create(
      IpPort::From_uint16_t(publication_ptr->port),
      fxl::To<std::vector<std::string>>(publication_ptr->text));
  publication->ptr_ttl_seconds = publication_ptr->ptr_ttl_seconds;
  publication->srv_ttl_seconds = publication_ptr->srv_ttl_seconds;
  publication->txt_ttl_seconds = publication_ptr->txt_ttl_seconds;

  return publication;
}

}  // namespace mdns
