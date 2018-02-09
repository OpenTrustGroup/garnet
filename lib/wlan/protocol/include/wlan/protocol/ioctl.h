// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_PROTOCOL_IOCTL_H
#define GARNET_LIB_WLAN_PROTOCOL_IOCTL_H

#include <zircon/compiler.h>
#include <zircon/device/ioctl.h>

__BEGIN_CDECLS

// Queries a wlanphy device for its capabilities.
// Returns a wlan.phy WlanInfo struct.
#define IOCTL_WLANPHY_QUERY IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_WLANPHY, 0)

// Creates a wlaniface device.
// in: wlan.phy CreateIfaceRequest
// out: wlan.iface WlanIface
#define IOCTL_WLANPHY_CREATE_IFACE IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_WLANPHY, 1)

// Destroys a wlaniface device.
// in: wlan.phy DestroyIfaceRequest
#define IOCTL_WLANPHY_DESTROY_IFACE IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_WLANPHY, 2)

__END_CDECLS

#endif  // GARNET_LIB_WLAN_PROTOCOL_IOCTL_H
