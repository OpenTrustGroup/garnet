// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package connectivity

import (
	"log"
	"sync"
	"syscall/zx"

	"app/context"
	"netstack/fidlconv"
	"netstack/netiface"

	"fidl/fuchsia/net"
	"fidl/fuchsia/netstack"
)

const debug = false

var service *net.ConnectivityService = &net.ConnectivityService{}
var reachable bool = false
var mu sync.Mutex

func AddOutgoingService(ctx *context.Context) error {
	ctx.OutgoingService.AddService(net.ConnectivityName, func(c zx.Channel) error {
		k, err := service.Add(struct{}{}, c, nil)
		// Let clients know the status of the network when they get added.
		if p, ok := service.EventProxyFor(k); ok {
			p.OnNetworkReachable(reachable)
		}
		return err
	})
	return nil
}

// TODO(NET-1001): extract into a separate reachability service based on a
// better network reachability signal.
func InferAndNotify(ifs []netstack.NetInterface) {
	if debug {
		log.Printf("inferring network reachability")
	}
	mu.Lock()
	current := inferReachability(ifs)
	if current != reachable {
		if debug {
			log.Printf("notifying clients of new reachability status: %t", current)
		}
		reachable = current
		notify(reachable)
	}
	mu.Unlock()
}

func hasDHCPAddress(nic netstack.NetInterface) bool {
	return nic.Flags&netstack.NetInterfaceFlagDhcp != 0 && nic.Flags&netstack.NetInterfaceFlagUp != 0 && !netiface.IsAny(fidlconv.NetAddressToTCPIPAddress(nic.Addr))
}

func inferReachability(ifs []netstack.NetInterface) bool {
	for _, nic := range ifs {
		if hasDHCPAddress(nic) {
			return true
		}
	}
	return false
}

func notify(reachable bool) {
	for key := range service.Bindings {
		if p, ok := service.EventProxyFor(key); ok {
			p.OnNetworkReachable(reachable)
		}
	}
}
