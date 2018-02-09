// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"log"
	"strings"

	"app/context"
	"fidl/bindings"
	"netstack/link/eth"
	"syscall/zx"
	"syscall/zx/mxerror"

	"garnet/public/lib/netstack/fidl/net_address"
	nsfidl "garnet/public/lib/netstack/fidl/netstack"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/transport/tcp"
	"github.com/google/netstack/tcpip/transport/udp"
)

type netstackImpl struct {
	listener *nsfidl.NotificationListener_Proxy
	stub     *bindings.Stub
}

func toTCPIPAddress(addr net_address.NetAddress) tcpip.Address {
	out := tcpip.Address("")
	switch addr.Family {
	case net_address.NetAddressFamily_Ipv4:
		out = tcpip.Address(addr.Ipv4[:])
	case net_address.NetAddressFamily_Ipv6:
		out = tcpip.Address(addr.Ipv6[:])
	}
	return out
}

func toNetAddress(addr tcpip.Address) net_address.NetAddress {
	out := net_address.NetAddress{Family: net_address.NetAddressFamily_Unspecified}
	switch len(addr) {
	case 4:
		out.Family = net_address.NetAddressFamily_Ipv4
		out.Ipv4 = &[4]uint8{}
		copy(out.Ipv4[:], addr[:])
	case 16:
		out.Family = net_address.NetAddressFamily_Ipv6
		out.Ipv6 = &[16]uint8{}
		copy(out.Ipv6[:], addr[:])
	}
	return out
}

func toSubnets(addrs []tcpip.Address) []net_address.Subnet {
	out := make([]net_address.Subnet, len(addrs))
	for i := range addrs {
		// TODO: prefix len?
		out[i] = net_address.Subnet{Addr: toNetAddress(addrs[i]), PrefixLen: 64}
	}
	return out
}

func getInterfaces() (out []nsfidl.NetInterface) {
	ns.mu.Lock()
	defer ns.mu.Unlock()

	index := uint32(0)
	for nicid, ifs := range ns.ifStates {
		// Long-hand for: broadaddr = ifs.nic.Addr | ^ifs.nic.Netmask
		broadaddr := []byte(ifs.nic.Addr)
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
			Name:      fmt.Sprintf("en%d", nicid),
			Addr:      toNetAddress(ifs.nic.Addr),
			Netmask:   toNetAddress(tcpip.Address(ifs.nic.Netmask)),
			Broadaddr: toNetAddress(tcpip.Address(broadaddr)),
			Hwaddr:    []uint8(ifs.nic.Mac[:]),
			Ipv6addrs: toSubnets(ifs.nic.Ipv6addrs),
		}

		out = append(out, outif)
		index++
	}
	return out
}

func (ni *netstackImpl) RegisterListener(listener *nsfidl.NotificationListener_Pointer) (err error) {
	if listener != nil {
		lp := nsfidl.NewProxyForNotificationListener(*listener, bindings.GetAsyncWaiter())
		ni.listener = lp
	}
	return nil
}

func (ni *netstackImpl) GetPortForService(service string, protocol nsfidl.Protocol) (port uint16, err error) {
	switch protocol {
	case nsfidl.Protocol_Udp:
		port, err = serviceLookup(service, udp.ProtocolNumber)
	case nsfidl.Protocol_Tcp:
		port, err = serviceLookup(service, tcp.ProtocolNumber)
	default:
		port, err = serviceLookup(service, tcp.ProtocolNumber)
		if err != nil {
			port, err = serviceLookup(service, udp.ProtocolNumber)
		}
	}
	return port, err
}

func (ni *netstackImpl) GetAddress(name string, port uint16) (out []net_address.SocketAddress, netErr nsfidl.NetErr, retErr error) {
	// TODO: This should handle IP address strings, empty strings, "localhost", etc. Pull the logic from
	// fdio's getaddrinfo into here.
	addrs, err := ns.dispatcher.dnsClient.LookupIP(name)
	if err == nil {
		out = make([]net_address.SocketAddress, len(addrs))
		netErr = nsfidl.NetErr{Status: nsfidl.Status_Ok}
		for i, addr := range addrs {
			switch len(addr) {
			case 4, 16:
				out[i].Addr = toNetAddress(addr)
				out[i].Port = port
			}
		}
	} else {
		netErr = nsfidl.NetErr{Status: nsfidl.Status_DnsError, Message: err.Error()}
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

func (ni *netstackImpl) GetAggregateStats() (stats nsfidl.AggregateStats, err error) {
	s := ns.stack.Stats()
	return nsfidl.AggregateStats{
		UnknownProtocolReceivedPackets:        s.UnknownProtocolRcvdPackets,
		UnknownNetworkEndpointReceivedPackets: s.UnknownNetworkEndpointRcvdPackets,
		MalformedReceivedPackets:              s.MalformedRcvdPackets,
		DroppedPackets:                        s.DroppedPackets,
		TcpStats: nsfidl.TcpStats{
			ActiveConnectionOpenings:  s.TCP.ActiveConnectionOpenings,
			PassiveConnectionOpenings: s.TCP.PassiveConnectionOpenings,
			FailedConnectionAttempts:  s.TCP.FailedConnectionAttempts,
			ValidSegmentsReceived:     s.TCP.ValidSegmentsReceived,
			InvalidSegmentsReceived:   s.TCP.InvalidSegmentsReceived,
			SegmentsSent:              s.TCP.SegmentsSent,
			ResetsSent:                s.TCP.ResetsSent,
		},
	}, nil
}

func (ni *netstackImpl) GetStats(nicid uint32) (stats nsfidl.NetInterfaceStats, err error) {
	// Pure reading of statistics. No critical section. No lock is needed.
	ifState, ok := ns.ifStates[tcpip.NICID(nicid)]

	if !ok {
		return nsfidl.NetInterfaceStats{}, fmt.Errorf("no such interface id: %d", nicid)
	}

	return ifState.statsEP.Stats, nil
}

func (ni *netstackImpl) SetInterfaceStatus(nicid uint32, enabled bool) (err error) {
	ifState, ok := ns.ifStates[tcpip.NICID(nicid)]

	if !ok {
		// TODO(mpcomplete): This will close the FIDL channel. Should fail more gracefully.
		return fmt.Errorf("no such interface id: %d", nicid)
	}

	if enabled {
		ifState.eth.Start()
	} else {
		ifState.eth.Down()
	}

	return nil
}

func (ni *netstackImpl) onInterfacesChanged(interfaces []nsfidl.NetInterface) {
	if ni.listener != nil {
		ni.listener.OnInterfacesChanged(interfaces)
	}
}

type netstackDelegate struct {
	clients []*netstackImpl
}

func remove(clients []*netstackImpl, client *netstackImpl) []*netstackImpl {
	for i, s := range clients {
		if s == client {
			clients[len(clients)-1], clients[i] = clients[i], clients[len(clients)-1]
			break
		}
	}
	return clients
}

func (delegate *netstackDelegate) Bind(request nsfidl.Netstack_Request) {
	client := &netstackImpl{}
	client.stub = request.NewStub(client, bindings.GetAsyncWaiter())
	delegate.clients = append(delegate.clients, client)
	go func() {
		for {
			if err := client.stub.ServeRequest(); err != nil {
				if mxerror.Status(err) != zx.ErrPeerClosed {
					log.Println(err)
				}
				break
			}
		}
		delegate.clients = remove(delegate.clients, client)
	}()
}

func (delegate *netstackDelegate) Quit() {
	for _, client := range delegate.clients {
		client.stub.Close()
	}
}

var netstackService *netstackDelegate

// AddNetstackService registers the NetstackService with the application context,
// allowing it to respond to FIDL queries.
func AddNetstackService(ctx *context.Context) error {
	if netstackService != nil {
		return fmt.Errorf("AddNetworkService must be called only once")
	}
	netstackService = &netstackDelegate{}
	ctx.OutgoingService.AddService(&nsfidl.Netstack_ServiceBinder{netstackService})
	return nil
}

func OnInterfacesChanged() {
	if netstackService != nil && netstackService.clients != nil {
		interfaces := getInterfaces()
		for _, client := range netstackService.clients {
			client.onInterfacesChanged(interfaces)
		}
	}
}
