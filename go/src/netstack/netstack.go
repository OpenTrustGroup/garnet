// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"errors"
	"fmt"
	"log"
	"sort"
	"strings"
	"sync"

	"netstack/dns"
	"netstack/filter"
	"netstack/link/eth"
	"netstack/link/stats"
	"netstack/netiface"

	"fidl/fuchsia/devicesettings"
	"fidl/zircon/ethernet"

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

const (
	deviceSettingsManagerNodenameKey = "DeviceName"
	defaultNodename                  = "fuchsia-unset-device-name"
)

// A Netstack tracks all of the running state of the network stack.
type Netstack struct {
	arena        *eth.Arena
	stack        *stack.Stack
	socketServer *socketServer

	deviceSettings *devicesettings.DeviceSettingsManagerInterface
	dnsClient      *dns.Client

	mu       sync.Mutex
	nodename string
	ifStates map[tcpip.NICID]*ifState

	countNIC tcpip.NICID

	filter *filter.Filter
}

type dhcpState struct {
	client  *dhcp.Client
	ctx     context.Context
	cancel  context.CancelFunc
	enabled bool
}

// Each ifState tracks the state of a network interface.
type ifState struct {
	ns     *Netstack
	ctx    context.Context
	cancel context.CancelFunc
	eth    *eth.Client
	state  eth.State
	dhcpState

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

func subnetRoute(addr tcpip.Address, mask tcpip.AddressMask, nicid tcpip.NICID) tcpip.Route {
	return tcpip.Route{
		Destination: applyMask(addr, mask),
		Mask:        tcpip.Address(mask),
		Gateway:     tcpip.Address(""),
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

func (ns *Netstack) removeInterfaceAddress(nic tcpip.NICID, protocol tcpip.NetworkProtocolNumber, addr tcpip.Address, prefixLen uint8) error {
	sn, err := toSubnet(addr, prefixLen)

	if err != nil {
		return fmt.Errorf("Error parsing subnet format for NIC ID %d, error: %s", nic, err)
	}

	hasSubnet, tcpipErr := ns.stack.ContainsSubnet(nic, sn)
	if tcpipErr != nil {
		return errors.New(fmt.Sprintf("Error finding subnet %s for NIC ID %d, error: %s", sn, nic, tcpipErr))
	}

	if hasSubnet {
		tcpipErr = ns.stack.RemoveSubnet(nic, sn)
		if tcpipErr != nil {
			return errors.New(fmt.Sprintf("Error removing subnet %s from NIC ID %d, error: %s", sn, nic, tcpipErr))
		}
	} else {
		return errors.New(fmt.Sprintf("No such subnet %s for NIC ID %d", sn, nic))
	}

	tcpipErr = ns.stack.RemoveAddress(nic, addr)
	if tcpipErr != nil {
		return fmt.Errorf("Error removing address %s from NIC ID %d, error: %s", addr, nic, tcpipErr)
	}

	ifs, ok := ns.ifStates[nic]
	if !ok {
		panic(fmt.Sprintf("Interface state table out of sync: NIC [%d] known to third_party/netstack not found in garnet/netstack", nic))
	}

	newAddr, newSubnet, err1 := ns.stack.GetMainNICAddress(nic, protocol)
	netmask := newSubnet.Mask()
	if netmask == "" {
		addressSize := int(len(newAddr) * 8)
		netmask = tcpip.CIDRMask(addressSize, addressSize)
	}
	ifs.staticAddressChanged(newAddr, netmask)
	if err1 != nil {
		return fmt.Errorf("Error querying NIC ID %d, error: %s", nic, err)
	}
	return nil
}

func toSubnet(address tcpip.Address, prefixLen uint8) (tcpip.Subnet, error) {
	m := tcpip.CIDRMask(int(prefixLen), int(len(address)*8))
	return tcpip.NewSubnet(address.Mask(m), m)
}

func (ns *Netstack) setInterfaceAddress(nic tcpip.NICID, protocol tcpip.NetworkProtocolNumber, addr tcpip.Address, prefixLen uint8) error {

	sn, err := toSubnet(addr, prefixLen)
	if err != nil {
		return fmt.Errorf("Error parsing subnet format for NIC ID %d, error: %s", nic, err)
	}
	tcpipErr := ns.stack.AddAddress(nic, protocol, addr)
	if tcpipErr != nil {
		return fmt.Errorf("Error adding address %s to NIC ID %d, error: %s", addr, nic, tcpipErr)
	}

	tcpipErr = ns.stack.AddSubnet(nic, protocol, sn)
	if tcpipErr != nil {
		return errors.New(fmt.Sprintf("Error adding subnet %s to NIC ID %d, error: %s", sn, nic, tcpipErr))
	}

	ifs, ok := ns.ifStates[nic]
	if !ok {
		panic(fmt.Sprintf("Interface state table out of sync: NIC [%d] known to third_party/netstack not found in garnet/netstack", nic))
	}

	ifs.staticAddressChanged(addr, sn.Mask())
	return nil
}

func (ifs *ifState) staticAddressChanged(newAddr tcpip.Address, netmask tcpip.AddressMask) {
	ifs.ns.mu.Lock()
	ifs.nic.Addr = newAddr
	ifs.nic.Netmask = netmask
	ifs.ns.mu.Unlock()

	OnInterfacesChanged()
}

func (ifs *ifState) dhcpAcquired(oldAddr, newAddr tcpip.Address, config dhcp.Config) {
	if oldAddr != "" && oldAddr != newAddr {
		log.Printf("NIC %s: DHCP IP %s expired", ifs.nic.Name, oldAddr)
	}
	if config.Error != nil {
		log.Printf("%v", config.Error)
		return
	}
	if newAddr == "" {
		log.Printf("NIC %s: DHCP could not acquire address", ifs.nic.Name)
		return
	}
	log.Printf("NIC %s: DHCP acquired IP %s for %s", ifs.nic.Name, newAddr, config.LeaseLength)
	log.Printf("NIC %s: Adding DNS servers: %v", ifs.nic.Name, config.DNS)

	// Update default route with new gateway.
	ifs.ns.mu.Lock()
	ifs.nic.Routes = defaultRouteTable(ifs.nic.ID, config.Gateway)
	ifs.nic.Routes = append(ifs.nic.Routes, subnetRoute(newAddr, config.SubnetMask, ifs.nic.ID))
	ifs.nic.Netmask = config.SubnetMask
	ifs.nic.Addr = newAddr
	ifs.nic.DNSServers = config.DNS
	ifs.ns.mu.Unlock()

	ifs.ns.stack.SetRouteTable(ifs.ns.flattenRouteTables())
	ifs.ns.dnsClient.SetRuntimeServers(ifs.ns.getRuntimeDNSServerRefs())

	OnInterfacesChanged()
}

func (ifs *ifState) setDHCPStatus(enabled bool) {
	ifs.ns.mu.Lock()
	defer ifs.ns.mu.Unlock()
	d := &ifs.dhcpState
	if enabled == d.enabled {
		return
	}
	if enabled {
		d.ctx, d.cancel = context.WithCancel(ifs.ctx)
		d.client.Run(d.ctx)
	} else if d.cancel != nil {
		d.cancel()
	}
	d.enabled = enabled
}

func (ifs *ifState) stateChange(s eth.State) {
	switch s {
	case eth.StateDown:
		ifs.onEthStop()
	case eth.StateClosed:
		ifs.onEthStop()
		ifs.ns.mu.Lock()
		delete(ifs.ns.ifStates, ifs.nic.ID)
		ifs.ns.mu.Unlock()
	case eth.StateStarted:
		// Only call `restarted` if we are not in the initial state (which means we're still starting).
		if ifs.state != eth.StateUnknown {
			ifs.onEthRestart()
		}
	}
	ifs.state = s
	// Note: This will fire again once DHCP succeeds.
	OnInterfacesChanged()
}

func (ifs *ifState) onEthRestart() {
	log.Printf("NIC %s: restarting", ifs.nic.Name)
	ifs.ns.mu.Lock()
	ifs.ctx, ifs.cancel = context.WithCancel(context.Background())
	ifs.nic.Routes = defaultRouteTable(ifs.nic.ID, "")
	ifs.ns.mu.Unlock()

	ifs.ns.stack.SetRouteTable(ifs.ns.flattenRouteTables())
	ifs.setDHCPStatus(true)
}

func (ifs *ifState) onEthStop() {
	log.Printf("NIC %s: stopped", ifs.nic.Name)
	if ifs.cancel != nil {
		ifs.cancel()
	}
	if ifs.dhcpState.cancel != nil {
		// TODO: consider remembering DHCP status
		ifs.setDHCPStatus(false)
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
	ifs.ns.dnsClient.SetRuntimeServers(ifs.ns.getRuntimeDNSServerRefs())
}

func (ns *Netstack) flattenRouteTables() []tcpip.Route {
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

// Return a slice of references to each NIC's DNS servers.
// The caller takes ownership of the returned slice.
func (ns *Netstack) getRuntimeDNSServerRefs() []*[]tcpip.Address {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	refs := make([]*[]tcpip.Address, 0, len(ns.ifStates))
	for _, ifs := range ns.ifStates {
		refs = append(refs, &ifs.nic.DNSServers)
	}
	return refs
}

func (ns *Netstack) getDNSServers() []tcpip.Address {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	defaultServers := ns.dnsClient.GetDefaultServers()
	uniqServers := make(map[tcpip.Address]struct{})
	for _, ifs := range ns.ifStates {
		for _, server := range ifs.nic.DNSServers {
			uniqServers[server] = struct{}{}
		}
	}

	out := make([]tcpip.Address, 0, len(defaultServers)+len(uniqServers))
	out = append(out, defaultServers...)
	for server := range uniqServers {
		out = append(out, server)
	}
	return out
}

func (ns *Netstack) addLoopback() error {
	const nicid = 1
	ctx, cancel := context.WithCancel(context.Background())
	nic := &netiface.NIC{
		ID:       nicid,
		Addr:     header.IPv4Loopback,
		Netmask:  tcpip.AddressMask(strings.Repeat("\xff", 4)),
		Features: ethernet.InfoFeatureLoopback,
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

	setNICName(nic)

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
	ns.countNIC++
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

func (ns *Netstack) Bridge(nics []tcpip.NICID) error {
	// TODO(stijlist): save bridge in netstack state as NetInterface
	// TODO(stijlist): initialize bridge context.Context & cancelFunc
	b, err := ns.stack.Bridge(nics)
	if err != nil {
		return errors.New(err.String())
	}

	for _, nicid := range nics {
		nic, ok := ns.ifStates[nicid]
		if !ok {
			panic("NIC known by netstack not in interface table")
		}
		if err := nic.eth.SetPromiscuousMode(true); err != nil {
			return err
		}
	}

	b.Enable()
	return nil
}

func (ns *Netstack) addEth(path string) error {
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
	linkID := stack.RegisterLinkEndpoint(ep)
	linkAddr := ep.LinkAddress()
	lladdr := ipv6.LinkLocalAddr(linkAddr)

	// LinkEndpoint chains:
	// Put sniffer as close as the NIC.
	if debug2 {
		// A wrapper LinkEndpoint should encapsulate the underlying one,
		// and manifest itself to 3rd party netstack.
		linkID = sniffer.New(linkID)
	}

	f := filter.New(ns.stack.PortManager)
	linkID = filter.NewEndpoint(f, linkID)
	ns.filter = f

	linkID = ifs.statsEP.Wrap(linkID)

	ns.mu.Lock()
	ifs.nic.Ipv6addrs = []tcpip.Address{lladdr}

	nicid := ns.countNIC + 1
	ifs.nic.ID = nicid
	ifs.nic.Features = client.Features
	setNICName(ifs.nic)

	ifs.nic.Routes = defaultRouteTable(nicid, "")
	ns.ifStates[nicid] = ifs
	ns.countNIC++
	ns.mu.Unlock()

	log.Printf("NIC %s added using ethernet device %q", ifs.nic.Name, path)

	if err := ns.stack.CreateNIC(nicid, linkID); err != nil {
		return fmt.Errorf("NIC %s: could not create NIC for %q: %v", ifs.nic.Name, path, err)
	}
	if err := ns.stack.AddAddress(nicid, arp.ProtocolNumber, arp.ProtocolAddress); err != nil {
		return fmt.Errorf("NIC %s: adding arp address failed: %v", ifs.nic.Name, err)
	}
	if err := ns.stack.AddAddress(nicid, ipv6.ProtocolNumber, lladdr); err != nil {
		return fmt.Errorf("NIC %s: adding link-local IPv6 %v failed: %v", ifs.nic.Name, lladdr, err)
	}
	snaddr := ipv6.SolicitedNodeAddr(lladdr)
	if err := ns.stack.AddAddress(nicid, ipv6.ProtocolNumber, snaddr); err != nil {
		return fmt.Errorf("NIC %s: adding solicited-node IPv6 %v (link-local IPv6 %v) failed: %v", ifs.nic.Name, snaddr, lladdr, err)
	}
	log.Printf("NIC %s: link-local IPv6: %v", ifs.nic.Name, lladdr)

	ifs.dhcpState.client = dhcp.NewClient(ns.stack, nicid, linkAddr, ifs.dhcpAcquired)

	// Add default route. This will get clobbered later when we get a DHCP response.
	ns.stack.SetRouteTable(ns.flattenRouteTables())

	// TODO(NET-298): Delete this condition after enabling multiple concurrent DHCP clients
	// in third_party/netstack.
	if client.Features&ethernet.InfoFeatureWlan != 0 {
		// WLAN: Upon 802.1X port open, the state change will ensue, which
		// will invoke the DHCP Client.
		return nil
	}

	status, err := client.GetStatus()
	if err != nil {
		return fmt.Errorf("failed to getStatus for MAC[%v], error %v", client.MAC, err)
	}

	if status == eth.LinkUp {
		ifs.setDHCPStatus(true)
	}
	return nil
}

func setNICName(nic *netiface.NIC) {
	if nic.Features&ethernet.InfoFeatureLoopback != 0 {
		nic.Name = "lo"
	} else if nic.Features&ethernet.InfoFeatureWlan != 0 {
		nic.Name = fmt.Sprintf("wlan%d", nic.ID)
	} else {
		nic.Name = fmt.Sprintf("en%d", nic.ID)
	}
}
