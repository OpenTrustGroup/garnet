// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mdns/service/host_name_resolver.h"

#include "garnet/bin/mdns/service/mdns_names.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_point.h"

namespace mdns {

HostNameResolver::HostNameResolver(MdnsAgent::Host* host,
                                   const std::string& host_name,
                                   fxl::TimePoint timeout,
                                   Mdns::ResolveHostNameCallback callback)
    : MdnsAgent(host),
      host_name_(host_name),
      host_full_name_(MdnsNames::LocalHostFullName(host_name)),
      timeout_(timeout),
      callback_(std::move(callback)) {
  FXL_DCHECK(callback_);
}

HostNameResolver::~HostNameResolver() {}

void HostNameResolver::Start(const std::string& host_full_name) {
  // Note that |host_full_name_| is the name we're trying to resolve, not the
  // name of the local host, which is the (ignored) parameter to this method.

  SendQuestion(std::make_shared<DnsQuestion>(host_full_name_, DnsType::kA));
  SendQuestion(std::make_shared<DnsQuestion>(host_full_name_, DnsType::kAaaa));

  PostTaskForTime(
      [this]() {
        if (callback_) {
          callback_(host_name_, v4_address_, v6_address_);
          callback_ = nullptr;
          RemoveSelf();
        }
      },
      timeout_);
}

void HostNameResolver::ReceiveResource(const DnsResource& resource,
                                       MdnsResourceSection section) {
  if (resource.name_.dotted_string_ != host_full_name_) {
    return;
  }

  if (resource.type_ == DnsType::kA) {
    v4_address_ = resource.a_.address_.address_;
  } else if (resource.type_ == DnsType::kAaaa) {
    v6_address_ = resource.aaaa_.address_.address_;
  }
}

void HostNameResolver::EndOfMessage() {
  FXL_DCHECK(callback_);

  if (v4_address_ || v6_address_) {
    callback_(host_name_, v4_address_, v6_address_);
    callback_ = nullptr;
    PostTaskForTime([this]() { RemoveSelf(); }, fxl::TimePoint::Now());
  }
}

void HostNameResolver::Quit() {
  FXL_DCHECK(callback_);
  callback_(host_name_, v4_address_, v6_address_);
  callback_ = nullptr;
  RemoveSelf();
}

}  // namespace mdns
