// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"log"
	"sort"
	"strings"

	"app/context"
	"fidl/bindings"
	"netstack/link/eth"
	"syscall/zx"

	nsfidl "fuchsia/go/netstack"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
)

type netstackImpl struct {
	listener nsfidl.NotificationListenerInterface
}

func toTCPIPAddress(addr nsfidl.NetAddress) tcpip.Address {
	out := tcpip.Address("")
	switch addr.Family {
	case nsfidl.NetAddressFamilyIpv4:
		out = tcpip.Address(addr.Ipv4.Addr[:])
	case nsfidl.NetAddressFamilyIpv6:
		out = tcpip.Address(addr.Ipv6.Addr[:])
	}
	return out
}

func toNetAddress(addr tcpip.Address) nsfidl.NetAddress {
	out := nsfidl.NetAddress{Family: nsfidl.NetAddressFamilyUnspecified}
	switch len(addr) {
	case 4:
		out.Family = nsfidl.NetAddressFamilyIpv4
		out.Ipv4 = &nsfidl.Ipv4Address{Addr: [4]uint8{}}
		copy(out.Ipv4.Addr[:], addr[:])
	case 16:
		out.Family = nsfidl.NetAddressFamilyIpv6
		out.Ipv6 = &nsfidl.Ipv6Address{Addr: [16]uint8{}}
		copy(out.Ipv6.Addr[:], addr[:])
	}
	return out
}

func toSubnets(addrs []tcpip.Address) []nsfidl.Subnet {
	out := make([]nsfidl.Subnet, len(addrs))
	for i := range addrs {
		// TODO: prefix len?
		out[i] = nsfidl.Subnet{Addr: toNetAddress(addrs[i]), PrefixLen: 64}
	}
	return out
}

func getInterfaces() (out []nsfidl.NetInterface) {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	for nicid, ifs := range ns.ifStates {
		// Long-hand for: broadaddr = ifs.nic.Addr | ^ifs.nic.Netmask
		broadaddr := []byte(ifs.nic.Addr)
		if len(ifs.nic.Netmask) != len(ifs.nic.Addr) {
			log.Printf("warning: mismatched netmask and address length for nic: %+v", ifs.nic)
			continue
		}

		for i := range broadaddr {
			broadaddr[i] |= ^ifs.nic.Netmask[i]
		}

		var flags uint32
		if ifs.state == eth.StateStarted {
			flags |= nsfidl.NetInterfaceFlagUp
		}

		outif := nsfidl.NetInterface{
			Id:        uint32(nicid),
			Flags:     flags,
			Features:  ifs.nic.Features,
			Name:      ifs.nic.Name,
			Addr:      toNetAddress(ifs.nic.Addr),
			Netmask:   toNetAddress(tcpip.Address(ifs.nic.Netmask)),
			Broadaddr: toNetAddress(tcpip.Address(broadaddr)),
			Hwaddr:    []uint8(ifs.nic.Mac[:]),
			Ipv6addrs: toSubnets(ifs.nic.Ipv6addrs),
		}

		out = append(out, outif)
	}
	sort.Slice(out[:], func(i, j int) bool {
		return out[i].Id < out[j].Id
	})
	return out
}

func (ni *netstackImpl) RegisterListener(listener nsfidl.NotificationListenerInterface) (err error) {
	if bindings.Proxy(listener).IsValid() {
		ni.listener = listener
	}
	return nil
}

func (ni *netstackImpl) GetPortForService(service string, protocol nsfidl.Protocol) (port uint16, err error) {
	switch protocol {
	case nsfidl.ProtocolUdp:
		port, err = serviceLookup(service, udp.ProtocolNumber)
	case nsfidl.ProtocolTcp:
		port, err = serviceLookup(service, tcp.ProtocolNumber)
	default:
		port, err = serviceLookup(service, tcp.ProtocolNumber)
		if err != nil {
			port, err = serviceLookup(service, udp.ProtocolNumber)
		}
	}
	return port, err
}

func (ni *netstackImpl) GetAddress(name string, port uint16) (out []nsfidl.SocketAddress, netErr nsfidl.NetErr, retErr error) {
	// TODO: This should handle IP address strings, empty strings, "localhost", etc. Pull the logic from
	// fdio's getaddrinfo into here.
	addrs, err := ns.dispatcher.dnsClient.LookupIP(name)
	if err == nil {
		out = make([]nsfidl.SocketAddress, len(addrs))
		netErr = nsfidl.NetErr{Status: nsfidl.StatusOk}
		for i, addr := range addrs {
			switch len(addr) {
			case 4, 16:
				out[i].Addr = toNetAddress(addr)
				out[i].Port = port
			}
		}
	} else {
		netErr = nsfidl.NetErr{Status: nsfidl.StatusDnsError, Message: err.Error()}
	}
	return out, netErr, nil
}

func (ni *netstackImpl) GetInterfaces() (out []nsfidl.NetInterface, err error) {
	return getInterfaces(), nil
}

func (ni *netstackImpl) GetNodeName() (out string, err error) {
	ns.mu.Lock()
	nodename := ns.nodename
	ns.mu.Unlock()
	return nodename, nil
}

func (ni *netstackImpl) GetRouteTable() (out []nsfidl.RouteTableEntry, err error) {
	ns.mu.Lock()
	table := ns.stack.GetRouteTable()
	ns.mu.Unlock()

	for _, route := range table {
		// Ensure that if any of the returned addresss are "empty",
		// they still have the appropriate NetAddressFamily.
		l := 0
		if len(route.Destination) > 0 {
			l = len(route.Destination)
		} else if len(route.Mask) > 0 {
			l = len(route.Destination)
		} else if len(route.Gateway) > 0 {
			l = len(route.Gateway)
		}
		dest := route.Destination
		mask := route.Mask
		gateway := route.Gateway
		if len(dest) == 0 {
			dest = tcpip.Address(strings.Repeat("\x00", l))
		}
		if len(mask) == 0 {
			mask = tcpip.Address(strings.Repeat("\x00", l))
		}
		if len(gateway) == 0 {
			gateway = tcpip.Address(strings.Repeat("\x00", l))
		}

		out = append(out, nsfidl.RouteTableEntry{
			Destination: toNetAddress(dest),
			Netmask:     toNetAddress(mask),
			Gateway:     toNetAddress(gateway),
			Nicid:       uint32(route.NIC),
		})
	}
	return out, nil
}

func (ni *netstackImpl) SetRouteTable(rt []nsfidl.RouteTableEntry) error {
	routes := []tcpip.Route{}
	for _, r := range rt {
		route := tcpip.Route{
			Destination: toTCPIPAddress(r.Destination),
			Mask:        toTCPIPAddress(r.Netmask),
			Gateway:     toTCPIPAddress(r.Gateway),
			NIC:         tcpip.NICID(r.Nicid),
		}
		routes = append(routes, route)
	}

	ns.mu.Lock()
	defer ns.mu.Unlock()
	ns.stack.SetRouteTable(routes)

	return nil
}

func toSubnet(address nsfidl.NetAddress, prefixLen uint64) (tcpip.Subnet, error) {
	// TODO: use tcpip.Address#mask after fuchsia/third_party/netstack and github/third_party/netstack are unified and #mask can be made public.
	a := []byte(toTCPIPAddress(address))
	m := tcpip.CIDRMask(int(prefixLen), int(len(a)*8))
	for i, _ := range a {
		a[i] = a[i] & m[i]
	}
	return tcpip.NewSubnet(tcpip.Address(a), m)
}

// Add address to the given network interface.
func (ni *netstackImpl) SetInterfaceAddress(nicid uint32, address nsfidl.NetAddress, prefixLen uint64) (result nsfidl.NetErr, endService error) {
	log.Printf("net address %+v", address)
	var protocol tcpip.NetworkProtocolNumber
	switch address.Family {
	case nsfidl.NetAddressFamilyIpv4:
		protocol = ipv4.ProtocolNumber
	case nsfidl.NetAddressFamilyIpv6:
		return nsfidl.NetErr{nsfidl.StatusIpv4Only, "IPv6 not yet supported for SetInterfaceAddress"}, nil
	}

	nic := tcpip.NICID(nicid)
	addr := toTCPIPAddress(address)
	sn, err := toSubnet(address, prefixLen)
	if err != nil {
		result = nsfidl.NetErr{nsfidl.StatusParseError, "Error applying subnet mask to interface address"}
		return result, nil
	}

	if err = ns.setInterfaceAddress(nic, protocol, addr, sn); err != nil {
		return nsfidl.NetErr{nsfidl.StatusUnknownError, err.Error()}, nil
	}
	return nsfidl.NetErr{nsfidl.StatusOk, ""}, nil
}

func (ni *netstackImpl) BridgeInterfaces(nicids []uint32) (nsfidl.NetErr, error) {
	nics := make([]tcpip.NICID, len(nicids))
	for i, n := range nicids {
		nics[i] = tcpip.NICID(n)
	}
	err := ns.Bridge(nics)
	if err != nil {
		return nsfidl.NetErr{Status: nsfidl.StatusUnknownError}, nil
	}
	return nsfidl.NetErr{Status: nsfidl.StatusOk}, nil
}

func (ni *netstackImpl) GetAggregateStats() (stats nsfidl.AggregateStats, err error) {
	s := ns.stack.Stats()
	return nsfidl.AggregateStats{
		UnknownProtocolReceivedPackets: s.UnknownProtocolRcvdPackets,
		MalformedReceivedPackets:       s.MalformedRcvdPackets,
		DroppedPackets:                 s.DroppedPackets,
		IpStats: nsfidl.IpStats{
			PacketsReceived:          s.IP.PacketsReceived,
			InvalidAddressesReceived: s.IP.InvalidAddressesReceived,
			PacketsDiscarded:         s.IP.PacketsDiscarded,
			PacketsDelivered:         s.IP.PacketsDelivered,
			PacketsSent:              s.IP.PacketsSent,
			OutgoingPacketErrors:     s.IP.OutgoingPacketErrors,
		},
		TcpStats: nsfidl.TcpStats{
			ActiveConnectionOpenings:  s.TCP.ActiveConnectionOpenings,
			PassiveConnectionOpenings: s.TCP.PassiveConnectionOpenings,
			FailedConnectionAttempts:  s.TCP.FailedConnectionAttempts,
			ValidSegmentsReceived:     s.TCP.ValidSegmentsReceived,
			InvalidSegmentsReceived:   s.TCP.InvalidSegmentsReceived,
			SegmentsSent:              s.TCP.SegmentsSent,
			ResetsSent:                s.TCP.ResetsSent,
		},
		UdpStats: nsfidl.UdpStats{
			PacketsReceived:          s.UDP.PacketsReceived,
			UnknownPortErrors:        s.UDP.UnknownPortErrors,
			ReceiveBufferErrors:      s.UDP.ReceiveBufferErrors,
			MalformedPacketsReceived: s.UDP.MalformedPacketsReceived,
			PacketsSent:              s.UDP.PacketsSent,
		},
	}, nil
}

func (ni *netstackImpl) GetStats(nicid uint32) (stats nsfidl.NetInterfaceStats, err error) {
	// Pure reading of statistics. No critical section. No lock is needed.
	ifState, ok := ns.ifStates[tcpip.NICID(nicid)]

	if !ok {
		// TODO(stijlist): refactor to return NetErr and use StatusUnknownInterface
		return nsfidl.NetInterfaceStats{}, fmt.Errorf("no such interface id: %d", nicid)
	}

	return ifState.statsEP.Stats, nil
}

func (ni *netstackImpl) SetInterfaceStatus(nicid uint32, enabled bool) (err error) {
	ifState, ok := ns.ifStates[tcpip.NICID(nicid)]

	if !ok {
		// TODO(stijlist): refactor to return NetErr and use StatusUnknownInterface
		return fmt.Errorf("no such interface id: %d", nicid)
	}

	if enabled {
		ifState.eth.Up()
	} else {
		ifState.eth.Down()
	}

	return nil
}

func (ni *netstackImpl) SetDhcpClientStatus(nicid uint32, enabled bool) (result nsfidl.NetErr, err error) {
	ifState, ok := ns.ifStates[tcpip.NICID(nicid)]
	if !ok {
		return nsfidl.NetErr{nsfidl.StatusUnknownInterface, "unknown interface"}, nil
	}
	ifState.setDHCPStatus(enabled)
	return nsfidl.NetErr{nsfidl.StatusOk, ""}, nil
}

func (ni *netstackImpl) onInterfacesChanged(interfaces []nsfidl.NetInterface) {
	if bindings.Proxy(ni.listener).IsValid() {
		ni.listener.OnInterfacesChanged(interfaces)
	}
}

var netstackService *bindings.BindingSet

// AddNetstackService registers the NetstackService with the application context,
// allowing it to respond to FIDL queries.
func AddNetstackService(ctx *context.Context) error {
	if netstackService != nil {
		return fmt.Errorf("AddNetworkService must be called only once")
	}
	netstackService = &bindings.BindingSet{}
	ctx.OutgoingService.AddService(nsfidl.NetstackName, func(c zx.Channel) error {
		_, err := netstackService.Add(&nsfidl.NetstackStub{
			Impl: &netstackImpl{},
		}, c, nil)
		return err
	})
	return nil
}

func OnInterfacesChanged() {
	if netstackService != nil {
		interfaces := getInterfaces()
		for _, client := range netstackService.Bindings {
			client.Stub.(*nsfidl.NetstackStub).Impl.(*netstackImpl).onInterfacesChanged(interfaces)
		}
	}
}
