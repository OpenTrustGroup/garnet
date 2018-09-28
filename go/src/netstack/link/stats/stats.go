// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package stats implements a LinkEndpoint of the netstack,
// with responsibility to keep track of traffic statistics.
// This LinkEndpoint may be inserted into any software layer as desired.
// One feasible insertaion point is between the link layer sniffer and
// the network protocol layer.

package stats

import (
	"log"
	"time"

	nsfidl "fidl/fuchsia/netstack"
	"netstack/netiface"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/buffer"
	"github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/stack"
)

const debug = false

type StatsEndpoint struct {
	dispatcher stack.NetworkDispatcher
	lower      stack.LinkEndpoint
	// Storage is made of FIDL data structure
	// enabling conversion-free export and import.
	Stats nsfidl.NetInterfaceStats
	Nic   *netiface.NIC
}

func (e *StatsEndpoint) Wrap(lower tcpip.LinkEndpointID) tcpip.LinkEndpointID {
	e.lower = stack.FindLinkEndpoint(lower)
	e.Stats = nsfidl.NetInterfaceStats{
		UpSince: time.Now().Unix(),
	}
	return stack.RegisterLinkEndpoint(e)
}

// DeliverNetworkPacket handles incoming packet from the lower layer.
// It performs packet inspection, and extract a rich set of statistics,
// and stores them to a FIDL data structure.
func (e *StatsEndpoint) DeliverNetworkPacket(linkEP stack.LinkEndpoint, dstLinkAddr, remoteLinkAddr tcpip.LinkAddress, protocol tcpip.NetworkProtocolNumber, vv buffer.VectorisedView) {
	e.analyzeTrafficStats(&e.Stats.Rx, protocol, vv.First(), vv.Size())
	e.dispatcher.DeliverNetworkPacket(e, dstLinkAddr, remoteLinkAddr, protocol, vv)
}

// Attach implements registration of lower endpoint and dispatcher.
func (e *StatsEndpoint) Attach(dispatcher stack.NetworkDispatcher) {
	e.dispatcher = dispatcher
	e.lower.Attach(e)
}

func (e *StatsEndpoint) Capabilities() stack.LinkEndpointCapabilities {
	return e.lower.Capabilities()
}

// MTU direcly directly uses lower layer MTU().
func (e *StatsEndpoint) MTU() uint32 {
	return e.lower.MTU()
}

// MaxHeaderLength directly uses lower layer MaxHeaderLength()
func (e *StatsEndpoint) MaxHeaderLength() uint16 {
	return e.lower.MaxHeaderLength()
}

// LinkAddress directly uses the lower layer LinkAddress()
func (e *StatsEndpoint) LinkAddress() tcpip.LinkAddress {
	return e.lower.LinkAddress()
}

func (e *StatsEndpoint) IsAttached() bool {
	return e.lower.IsAttached()
}

// WritePacket handles outgoing packet from the higher layer.
// It performs packet inspection, and extracts a rich set of statistics,
// and stores them to a FIDL data structure.
func (e *StatsEndpoint) WritePacket(r *stack.Route, hdr buffer.Prependable, payload buffer.VectorisedView, protocol tcpip.NetworkProtocolNumber) *tcpip.Error {
	e.analyzeTrafficStats(&e.Stats.Tx, protocol, hdr.View(), hdr.UsedLength()+payload.Size())
	return e.lower.WritePacket(r, hdr, payload, protocol)
}

func (e *StatsEndpoint) analyzeTrafficStats(ts *nsfidl.NetTrafficStats, protocol tcpip.NetworkProtocolNumber, hdr []byte, packetSize int) {
	ts.PktsTotal += 1
	ts.BytesTotal += uint64(packetSize)

	switch protocol {
	case header.IPv4ProtocolNumber:
		ipPkt := header.IPv4(hdr)
		if ipPkt.IsValid(packetSize) {
			e.analyzeIPv4(ts, ipPkt)
		}

	case header.IPv6ProtocolNumber:
		ipPkt := header.IPv6(hdr)
		if ipPkt.IsValid(packetSize) {
			e.analyzeIPv6(ts, ipPkt)
		}

		// Add other protocol below.
	}
}

func (e *StatsEndpoint) analyzeIPv4(ts *nsfidl.NetTrafficStats, ipPkt header.IPv4) {
	mcastToSelf := header.IsV4MulticastAddress(ipPkt.DestinationAddress()) && e.IsV4FromSelf(ipPkt)

	// Add conditions not to collect stats in.
	if mcastToSelf {
		if debug {
			log.Printf("[stats] detected multicast-to-self at NICID %v: %v -> %v", e.Nic.ID, ipPkt.SourceAddress(), ipPkt.DestinationAddress())
		}
		return
	}

	ipPayload := ipPkt[ipPkt.HeaderLength():]

	nextProtocol := ipPkt.Protocol()
	switch tcpip.TransportProtocolNumber(nextProtocol) {
	case header.ICMPv4ProtocolNumber:
		icmpPkt := header.ICMPv4(ipPayload)
		switch icmpPkt.Type() {
		case header.ICMPv4Echo:
			ts.PktsEchoReq += 1
		case header.ICMPv4EchoReply:
			ts.PktsEchoRep += 1
		}
	}
}

func (e *StatsEndpoint) analyzeIPv6(ts *nsfidl.NetTrafficStats, ipPkt header.IPv6) {
	mcastToSelf := header.IsV6MulticastAddress(ipPkt.DestinationAddress()) && e.IsV6FromSelf(ipPkt)

	// Add conditions not to collect stats in.
	if mcastToSelf {
		if debug {
			log.Printf("[stats] detected multicast-to-self at NICID %v: %v -> %v", e.Nic.ID, ipPkt.SourceAddress(), ipPkt.DestinationAddress())
		}
		return
	}

	// TODO(porce): Fix this naive assumption by processing
	// all optional headers first before accessing ICMPv6 packet.
	ipPayload := ipPkt[header.IPv6MinimumSize:]

	nextProtocol := ipPkt.NextHeader()
	switch tcpip.TransportProtocolNumber(nextProtocol) {
	case header.ICMPv6ProtocolNumber:
		icmpPkt := header.ICMPv6(ipPayload)
		switch icmpPkt.Type() {
		case header.ICMPv6EchoRequest:
			ts.PktsEchoReqV6 += 1
		case header.ICMPv6EchoReply:
			ts.PktsEchoRepV6 += 1
		}
	}
}

func (e *StatsEndpoint) IsV4FromSelf(ipPkt header.IPv4) bool {
	return e.Nic.Addr == ipPkt.SourceAddress()
}

func (e *StatsEndpoint) IsV6FromSelf(ipPkt header.IPv6) bool {
	addr := ipPkt.SourceAddress()
	for _, a := range e.Nic.Ipv6addrs {
		if a == addr {
			return true
		}
	}
	return false
}
