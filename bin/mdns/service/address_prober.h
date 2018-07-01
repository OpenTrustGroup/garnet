// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_SERVICE_ADDRESS_PROBER_H_
#define GARNET_BIN_MDNS_SERVICE_ADDRESS_PROBER_H_

#include "garnet/bin/mdns/service/prober.h"

#include <lib/fit/function.h>

namespace mdns {

// Probes for host name conflicts prior to invoking |AddressResponder|.
class AddressProber : public Prober {
 public:
  using CompletionCallback = fit::function<void(bool)>;

  // Creates an |AddressProber|.
  AddressProber(MdnsAgent::Host* host, CompletionCallback callback);

  ~AddressProber() override;

 protected:
  // Prober overrides.
  const std::string& ResourceName() override;

  void SendProposedResources(MdnsResourceSection section) override;
};

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_SERVICE_ADDRESS_PROBER_H_
