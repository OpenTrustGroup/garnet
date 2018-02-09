// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mdns/standalone/ioctl_interface_monitor.h"

#include <errno.h>
#include <sys/socket.h>

#include "garnet/bin/mdns/service/ip_address.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"
#include "lib/netstack/c/netconfig.h"

namespace mdns {
namespace {

constexpr fxl::TimeDelta kPollInterval = fxl::TimeDelta::FromSeconds(60);

}  // namespace

// static
std::unique_ptr<InterfaceMonitor> IoctlInterfaceMonitor::Create() {
  return std::unique_ptr<InterfaceMonitor>(new IoctlInterfaceMonitor());
}

IoctlInterfaceMonitor::IoctlInterfaceMonitor()
    : poll_closure_([this]() { Poll(); }) {
  CheckInterfaces();
  fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      poll_closure_.callback(), kPollInterval);
}

IoctlInterfaceMonitor::~IoctlInterfaceMonitor() {}

void IoctlInterfaceMonitor::RegisterLinkChangeCallback(
    const fxl::Closure& callback) {
  link_change_callback_ = callback;
}

const std::vector<std::unique_ptr<InterfaceDescriptor>>&
IoctlInterfaceMonitor::GetInterfaces() {
  return interfaces_;
}

void IoctlInterfaceMonitor::Poll() {
  if (CheckInterfaces() && link_change_callback_) {
    link_change_callback_();
  }

  fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      poll_closure_.callback(), kPollInterval);
}

bool IoctlInterfaceMonitor::CheckInterfaces() {
  bool link_change = false;

  fxl::UniqueFD socket_fd = fxl::UniqueFD(socket(AF_INET6, SOCK_STREAM, 0));
  if (!socket_fd.is_valid()) {
    FXL_LOG(ERROR) << "Failed to open socket, errno " << errno;
    return true;
  }

  netc_get_if_info_t if_infos;
  ssize_t size = ioctl_netc_get_if_info(socket_fd.get(), &if_infos);

  if (size < 0 || if_infos.n_info == 0) {
    return true;
  }

  for (uint32_t i = 0; i < if_infos.n_info; ++i) {
    netc_if_info_t* if_info = &if_infos.info[i];

    IpAddress address(if_info->addr);

    if (!address.is_valid() || address.is_loopback() ||
        (if_info->flags & NETC_IFF_UP) == 0) {
      if (interfaces_.size() > if_info->index &&
          interfaces_[if_info->index] != nullptr) {
        // Interface went away.
        interfaces_[if_info->index] = nullptr;
        link_change = true;
      }

      continue;
    }

    // Make sure the |interfaces_| array is big enough.
    if (interfaces_.size() <= if_info->index) {
      interfaces_.resize(if_info->index + 1);
    }

    // Add a descriptor if we don't already have one.
    if (interfaces_[if_info->index] == nullptr) {
      interfaces_[if_info->index].reset(
          new InterfaceDescriptor(address, if_info->name));
      link_change = true;
    }
  }

  if (link_change && link_change_callback_) {
    link_change_callback_();
  }

  return false;
}

}  // namespace mdns
