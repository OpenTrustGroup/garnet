// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_SERVICE_INSTANCE_PROBER_H_
#define GARNET_BIN_MDNS_SERVICE_INSTANCE_PROBER_H_

#include <lib/fit/function.h>

#include "garnet/bin/mdns/service/prober.h"

namespace mdns {

// Probes for SRV record conflicts prior to invoking |InstanceResponder|.
class InstanceProber : public Prober {
 public:
  using CompletionCallback = fit::function<void(bool)>;

  // Creates a |InstanceProber|.
  InstanceProber(MdnsAgent::Host* host, const std::string& service_name,
                 const std::string& instance_name, IpPort port,
                 CompletionCallback callback);

  ~InstanceProber() override;

 protected:
  // Prober overrides.
  const std::string& ResourceName() override;

  void SendProposedResources(MdnsResourceSection section) override;

 private:
  std::string instance_full_name_;
  IpPort port_;
};

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_SERVICE_INSTANCE_PROBER_H_
