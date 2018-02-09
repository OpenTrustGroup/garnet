// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"log"
	"sort"
	"strings"
	"sync"

	"netstack/deviceid"
	"netstack/link/eth"
	"netstack/link/stats"
	"netstack/netiface"

	"github.com/google/netstack/dhcp"
	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/link/loopback"
	"github.com/google/netstack/tcpip/link/sniffer"
	"github.com/google/netstack/tcpip/network/arp"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/network/ipv6"
	"github.com/google/netstack/tcpip/stack"
)

// A netstack tracks all of the running state of the network stack.
type netstack struct {
	arena      *eth.Arena
	stack      *stack.Stack
	dispatcher *socketServer

	mu       sync.Mutex
	nodename string
	ifStates map[tcpip.NICID]*ifState
}

// Each ifState tracks the state of a network interface.
type ifState struct {
	ns     *netstack
	ctx    context.Context
	cancel context.CancelFunc
	eth    *eth.Client
	dhcp   *dhcp.Client
	state  eth.State

	// guarded by ns.mu
	// NIC is defined in //garnet/go/src/netstack/netiface/netiface.go
	// TODO(porce): Consider replacement with //third_party/netstack/tcpip/stack/stack.go
	nic *netiface.NIC

	// LinkEndpoint responsible to track traffic statistics
	statsEP stats.StatsEndpoint
}

func defaultRouteTable(nicid tcpip.NICID, gateway tcpip.Address) []tcpip.Route {
	return []tcpip.Route{
		{
			Destination: tcpip.Address(strings.Repeat("\x00", 4)),
			Mask:        tcpip.Address(strings.Repeat("\x00", 4)),
			Gateway:     gateway,
			NIC:         nicid,
		},
		{
			Destination: tcpip.Address(strings.Repeat("\x00", 16)),
			Mask:        tcpip.Address(strings.Repeat("\x00", 16)),
			NIC:         nicid,
		},
	}
}

func subnetRoute(addr tcpip.Address, mask tcpip.AddressMask, nicid tcpip.NICID, gateway tcpip.Address) tcpip.Route {
	return tcpip.Route{
		Destination: applyMask(addr, mask),
		Mask:        tcpip.Address(mask),
		Gateway:     gateway,
		NIC:         nicid,
	}
}

func applyMask(addr tcpip.Address, mask tcpip.AddressMask) tcpip.Address {
	if len(addr) != len(mask) {
		return ""
	}
	subnet := []byte(addr)
	for i := 0; i < len(subnet); i++ {
		subnet[i] &= mask[i]
	}
	return tcpip.Address(subnet)
}

func (ifs *ifState) dhcpAcquired(oldAddr, newAddr tcpip.Address, config dhcp.Config) {
	if oldAddr != "" && oldAddr != newAddr {
		log.Printf("NIC %d: DHCP IP %s expired", ifs.nic.ID, oldAddr)
	}
	if config.Error != nil {
		log.Printf("%v", config.Error)
		return
	}
	if newAddr == "" {
		log.Printf("NIC %d: DHCP could not acquire address", ifs.nic.ID)
		return
	}
	log.Printf("NIC %d: DHCP acquired IP %s for %s", ifs.nic.ID, newAddr, config.LeaseLength)
	log.Printf("NIC %d: DNS servers: %v", ifs.nic.ID, config.DNS)

	// Update default route with new gateway.
	ifs.ns.mu.Lock()
	ifs.nic.Routes = defaultRouteTable(ifs.nic.ID, config.Gateway)
	ifs.nic.Routes = append(ifs.nic.Routes, subnetRoute(newAddr, config.SubnetMask, ifs.nic.ID, config.Gateway))
	ifs.nic.Netmask = config.SubnetMask
	ifs.nic.Addr = newAddr
	ifs.nic.DNSServers = config.DNS
	ifs.ns.mu.Unlock()

	ifs.ns.stack.SetRouteTable(ifs.ns.flattenRouteTables())
	ifs.ns.dispatcher.dnsClient.SetRuntimeServers(ifs.ns.flattenDNSServers())

	OnInterfacesChanged()
}

func (ifs *ifState) stateChange(s eth.State) {
	switch s {
	case eth.StateClosed, eth.StateDown:
		ifs.stop()
	case eth.StateStarted:
		// Only restart if we are not in the initial state (which means we're still starting).
		if ifs.state != eth.StateUnknown {
			ifs.restart()
		}
	}
	ifs.state = s
	// Note: This will fire again once DHCP succeeds.
	OnInterfacesChanged()
}

func (ifs *ifState) restart() {
	log.Printf("NIC %d: restarting", ifs.nic.ID)
	ifs.ns.mu.Lock()
	ifs.ctx, ifs.cancel = context.WithCancel(context.Background())
	ifs.nic.Routes = defaultRouteTable(ifs.nic.ID, "")
	ifs.ns.mu.Unlock()

	ifs.ns.stack.SetRouteTable(ifs.ns.flattenRouteTables())

	go ifs.dhcp.Run(ifs.ctx)
}

func (ifs *ifState) stop() {
	log.Printf("NIC %d: stopped", ifs.nic.ID)
	if ifs.cancel != nil {
		ifs.cancel()
	}

	// TODO(crawshaw): more cleanup to be done here:
	// 	- remove link endpoint
	//	- reclaim NICID?

	ifs.ns.mu.Lock()
	ifs.nic.Routes = nil
	ifs.nic.Netmask = ""
	ifs.nic.Addr = ""
	ifs.nic.DNSServers = nil
	ifs.ns.mu.Unlock()

	ifs.ns.stack.SetRouteTable(ifs.ns.flattenRouteTables())
	ifs.ns.dispatcher.dnsClient.SetRuntimeServers(ifs.ns.flattenDNSServers())
}

func (ns *netstack) flattenRouteTables() []tcpip.Route {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	routes := make([]tcpip.Route, 0)
	nics := make(map[tcpip.NICID]*netiface.NIC)
	for _, ifs := range ns.ifStates {
		routes = append(routes, ifs.nic.Routes...)
		nics[ifs.nic.ID] = ifs.nic
	}
	sort.Slice(routes, func(i, j int) bool {
		return netiface.Less(&routes[i], &routes[j], nics)
	})
	if debug2 {
		for i, ifs := range ns.ifStates {
			log.Printf("[%v] nicid: %v, addr: %v, routes: %v",
				i, ifs.nic.ID, ifs.nic.Addr, ifs.nic.Routes)
		}
	}

	return routes
}

func (ns *netstack) flattenDNSServers() []tcpip.Address {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	uniqServers := make(map[tcpip.Address]struct{})
	for _, ifs := range ns.ifStates {
		for _, server := range ifs.nic.DNSServers {
			uniqServers[server] = struct{}{}
		}
	}
	servers := []tcpip.Address{}
	for server := range uniqServers {
		servers = append(servers, server)
	}
	return servers
}

func (ns *netstack) addLoopback() error {
	const nicid = 1
	ctx, cancel := context.WithCancel(context.Background())
	nic := &netiface.NIC{
		ID:      nicid,
		Addr:    header.IPv4Loopback,
		Netmask: tcpip.AddressMask(strings.Repeat("\xff", 4)),
		Routes: []tcpip.Route{
			{
				Destination: header.IPv4Loopback,
				Mask:        tcpip.Address(strings.Repeat("\xff", 4)),
				NIC:         nicid,
			},
			{
				Destination: header.IPv6Loopback,
				Mask:        tcpip.Address(strings.Repeat("\xff", 16)),
				NIC:         nicid,
			},
		},
	}

	ifs := &ifState{
		ns:     ns,
		ctx:    ctx,
		cancel: cancel,
		nic:    nic,
		state:  eth.StateStarted,
	}
	ifs.statsEP.Nic = ifs.nic

	ns.mu.Lock()
	if len(ns.ifStates) > 0 {
		ns.mu.Unlock()
		return fmt.Errorf("loopback: other interfaces already registered")
	}
	ns.ifStates[nicid] = ifs
	ns.mu.Unlock()

	linkID := loopback.New()
	if debug2 {
		linkID = sniffer.New(linkID)
	}
	linkID = ifs.statsEP.Wrap(linkID)

	if err := ns.stack.CreateNIC(nicid, linkID); err != nil {
		return fmt.Errorf("loopback: could not create interface: %v", err)
	}
	if err := ns.stack.AddAddress(nicid, ipv4.ProtocolNumber, header.IPv4Loopback); err != nil {
		return fmt.Errorf("loopback: adding ipv4 address failed: %v", err)
	}
	if err := ns.stack.AddAddress(nicid, ipv6.ProtocolNumber, header.IPv6Loopback); err != nil {
		return fmt.Errorf("loopback: adding ipv6 address failed: %v", err)
	}

	ns.stack.SetRouteTable(ns.flattenRouteTables())

	return nil
}

func (ns *netstack) addEth(path string) error {
	ctx, cancel := context.WithCancel(context.Background())

	ifs := &ifState{
		ns:     ns,
		ctx:    ctx,
		cancel: cancel,
		nic:    &netiface.NIC{},
		state:  eth.StateUnknown,
	}
	ifs.statsEP.Nic = ifs.nic

	client, err := eth.NewClient("netstack", path, ns.arena, ifs.stateChange)
	if err != nil {
		return err
	}
	ifs.eth = client
	ep := eth.NewLinkEndpoint(client)
	if err := ep.Init(); err != nil {
		log.Fatalf("%s: endpoint init failed: %v", path, err)
	}
	linkID := stack.RegisterLinkEndpoint(ep)
	lladdr := ipv6.LinkLocalAddr(tcpip.LinkAddress(ep.LinkAddr))

	// LinkEndpoint chains:
	// Put sniffer as close as the NIC.
	if debug2 {
		// A wrapper LinkEndpoint should encapsulate the underlying one,
		// and manifest itself to 3rd party netstack.
		linkID = sniffer.New(linkID)
	}
	linkID = ifs.statsEP.Wrap(linkID)

	ns.mu.Lock()
	ifs.nic.Ipv6addrs = []tcpip.Address{lladdr}
	copy(ifs.nic.Mac[:], ep.LinkAddr)

	var nicid tcpip.NICID
	for _, ifs := range ns.ifStates {
		if ifs.nic.ID > nicid {
			nicid = ifs.nic.ID
		}
	}
	nicid++
	if err := ns.stack.CreateNIC(nicid, linkID); err != nil {
		ns.mu.Unlock()
		return fmt.Errorf("NIC %d: could not create NIC for %q: %v", nicid, path, err)
	}
	if nicid == 2 && ns.nodename == "" {
		// This is the first real ethernet device on this host.
		// No nodename has been configured for the network stack,
		// so derive it from the MAC address.
		ns.nodename = deviceid.DeviceID(ifs.nic.Mac)
	}
	ifs.nic.ID = nicid
	ifs.nic.Routes = defaultRouteTable(nicid, "")
	ns.ifStates[nicid] = ifs
	ns.mu.Unlock()

	log.Printf("NIC %d added using ethernet device %q", nicid, path)
	log.Printf("NIC %d: ipv6addr: %v", nicid, lladdr)

	if err := ns.stack.AddAddress(nicid, arp.ProtocolNumber, arp.ProtocolAddress); err != nil {
		return fmt.Errorf("NIC %d: adding arp address failed: %v", nicid, err)
	}
	if err := ns.stack.AddAddress(nicid, ipv6.ProtocolNumber, lladdr); err != nil {
		return fmt.Errorf("NIC %d: adding link-local IPv6 failed: %v", nicid, err)
	}
	snaddr := ipv6.SolicitedNodeAddr(lladdr)
	if err := ns.stack.AddAddress(nicid, ipv6.ProtocolNumber, snaddr); err != nil {
		return fmt.Errorf("NIC %d: adding solicited-node IPv6 failed: %v", nicid, err)
	}

	// TODO(): Start DHCP Client after
	// (1) link is on
	// (2) its IP address is not to be statically configured
	// (3) Use of DHCP is explicitly configured
	ifs.dhcp = dhcp.NewClient(ns.stack, nicid, ep.LinkAddr, ifs.dhcpAcquired)

	// Add default route. This will get clobbered later when we get a DHCP response.
	ns.stack.SetRouteTable(ns.flattenRouteTables())

	// TODO(porce): Delete this condition. Treat wired ethernet, WLAN NICs in the same way.
	if client.Features&eth.FeatureWlan != 0 {
		// WLAN: Upon 802.1X port open, the state change will ensue, which
		// will invoke the DHCP Client.
		return nil
	}

	go ifs.dhcp.Run(ifs.ctx)
	return nil
}
