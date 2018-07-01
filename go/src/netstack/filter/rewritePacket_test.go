// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

import (
	"testing"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/header"
)

func TestRewritePacketICMPv4(t *testing.T) {
	var tests = []struct {
		packet   func() []byte
		newAddr  tcpip.Address
		isSource bool
	}{
		{
			func() []byte {
				return icmpV4Packet([]byte("payload."), &icmpV4Params{
					srcAddr:    "\x0a\x00\x00\x00",
					dstAddr:    "\x0a\x00\x00\x02",
					icmpV4Type: header.ICMPv4EchoReply,
					code:       0,
				})
			},
			"\x0b\x00\x00\x00",
			true,
		},
	}

	for _, test := range tests {
		b := test.packet()
		ipv4 := header.IPv4(b)
		th := b[ipv4.HeaderLength():]
		icmpv4 := header.ICMPv4(th)

		// Make sure the checksum in the original packet is correct.
		iCksum := ipv4.CalculateChecksum()
		if got, want := iCksum, uint16(0xffff); got != want {
			t.Errorf("ipv4 checksum=%x, want=%x", got, want)
		}

		tCksum := icmpv4.CalculateChecksum(0)
		if got, want := tCksum, uint16(0xffff); got != want {
			t.Errorf("icmpv4 checksum=%x, want=%x", got, want)
		}

		rewritePacketICMPv4(test.newAddr, test.isSource, b, th)

		if test.isSource {
			if got, want := ipv4.SourceAddress(), test.newAddr; got != want {
				t.Errorf("ipv4.SourceAddress()=%v, want=%v", got, want)
			}
		} else {
			if got, want := ipv4.DestinationAddress(), test.newAddr; got != want {
				t.Errorf("ipv4.DestinationAddress()=%v, want=%v", got, want)
			}
		}

		// Check if the checksum in the rewritten packet is correct.
		iCksum = ipv4.CalculateChecksum()
		if got, want := iCksum, uint16(0xffff); got != want {
			t.Errorf("ipv4 checksum=%x, want=%x", got, want)
		}

		tCksum = icmpv4.CalculateChecksum(0)
		if got, want := tCksum, uint16(0xffff); got != want {
			t.Errorf("icmpv4 checksum=%x, want=%x", got, want)
		}
	}
}

func TestRewritePacketUDPv4(t *testing.T) {
	var tests = []struct {
		packet   func() []byte
		newAddr  tcpip.Address
		newPort  uint16
		isSource bool
	}{
		{
			func() []byte {
				return udpV4Packet([]byte("payload"), &udpParams{
					srcAddr: "\x0a\x00\x00\x00",
					srcPort: 100,
					dstAddr: "\x0a\x00\x00\x02",
					dstPort: 200,
				})
			},
			"\x0b\x00\x00\x00",
			101,
			true,
		},
		{
			func() []byte {
				return udpV4Packet([]byte("payload"), &udpParams{
					srcAddr:       "\x0a\x00\x00\x00",
					srcPort:       100,
					dstAddr:       "\x0a\x00\x00\x02",
					dstPort:       200,
					noUDPChecksum: true,
				})
			},
			"\x0b\x00\x00\x00",
			101,
			false,
		},
	}

	for _, test := range tests {
		b := test.packet()
		ipv4 := header.IPv4(b)
		th := b[ipv4.HeaderLength():]
		udp := header.UDP(th)

		// Make sure the checksum in the original packet is correct.
		iCksum := ipv4.CalculateChecksum()
		if got, want := iCksum, uint16(0xffff); got != want {
			t.Errorf("ipv4 checksum=%x, want=%x", got, want)
		}

		noUDPChecksum := false
		if udp.Checksum() == 0 {
			noUDPChecksum = true
		} else {
			tCksum := header.PseudoHeaderChecksum(header.UDPProtocolNumber,
				ipv4.SourceAddress(), ipv4.DestinationAddress())
			tCksum = header.Checksum(udp.Payload(), tCksum)
			tCksum = udp.CalculateChecksum(tCksum, uint16(len(th)))
			if got, want := tCksum, uint16(0xffff); got != want {
				t.Errorf("udp checksum=%x, want=%x", got, want)
			}
		}

		rewritePacketUDPv4(test.newAddr, test.newPort, test.isSource, b, th)

		if test.isSource {
			if got, want := ipv4.SourceAddress(), test.newAddr; got != want {
				t.Errorf("ipv4.SourceAddress()=%v, want=%v", got, want)
			}
			if got, want := udp.SourcePort(), test.newPort; got != want {
				t.Errorf("ipv4.SourcePort()=%v, want=%v", got, want)
			}
		} else {
			if got, want := ipv4.DestinationAddress(), test.newAddr; got != want {
				t.Errorf("ipv4.DestinationAddress()=%v, want=%v", got, want)
			}
			if got, want := udp.DestinationPort(), test.newPort; got != want {
				t.Errorf("ipv4.DestinationPort()=%v, want=%v", got, want)
			}
		}

		// Check if the checksum in the rewritten packet is correct.
		iCksum = ipv4.CalculateChecksum()
		if got, want := iCksum, uint16(0xffff); got != want {
			t.Errorf("ipv4 checksum=%x, want=%x", got, want)
		}

		if noUDPChecksum {
			if got, want := udp.Checksum(), uint16(0); got != want {
				t.Errorf("udp checksum=%x, want=%x", got, want)
			}
		} else {
			tCksum := header.PseudoHeaderChecksum(header.UDPProtocolNumber,
				ipv4.SourceAddress(), ipv4.DestinationAddress())
			tCksum = header.Checksum(udp.Payload(), tCksum)
			tCksum = udp.CalculateChecksum(tCksum, uint16(len(th)))
			if got, want := tCksum, uint16(0xffff); got != want {
				t.Errorf("udp checksum=%x, want=%x", got, want)
			}
		}
	}
}

func TestRewritePacketTCPv4(t *testing.T) {
	var tests = []struct {
		packet   func() []byte
		newAddr  tcpip.Address
		newPort  uint16
		isSource bool
	}{
		{
			func() []byte {
				return tcpV4Packet([]byte("payload"), &tcpParams{
					srcAddr: "\x0a\x00\x00\x00",
					srcPort: 100,
					dstAddr: "\x0a\x00\x00\x02",
					dstPort: 200,
				})
			},
			"\x0b\x00\x00\x00",
			101,
			true,
		},
		{
			func() []byte {
				return tcpV4Packet([]byte("payload"), &tcpParams{
					srcAddr: "\x0a\x00\x00\x00",
					srcPort: 100,
					dstAddr: "\x0a\x00\x00\x02",
					dstPort: 200,
				})
			},
			"\x0b\x00\x00\x00",
			101,
			false,
		},
	}

	for _, test := range tests {
		b := test.packet()
		ipv4 := header.IPv4(b)
		th := b[ipv4.HeaderLength():]
		tcp := header.TCP(th)

		// Make sure the checksum in the original packet is correct.
		iCksum := ipv4.CalculateChecksum()
		if got, want := iCksum, uint16(0xffff); got != want {
			t.Errorf("ipv4 checksum=%x, want=%x", got, want)
		}

		tCksum := header.PseudoHeaderChecksum(header.TCPProtocolNumber,
			ipv4.SourceAddress(), ipv4.DestinationAddress())
		tCksum = header.Checksum(tcp.Payload(), tCksum)
		tCksum = tcp.CalculateChecksum(tCksum, uint16(len(th)))
		if got, want := tCksum, uint16(0xffff); got != want {
			t.Errorf("tcp checksum=%x, want=%x", got, want)
		}

		rewritePacketTCPv4(test.newAddr, test.newPort, test.isSource, b, th)

		if test.isSource {
			if got, want := ipv4.SourceAddress(), test.newAddr; got != want {
				t.Errorf("ipv4.SourceAddress()=%v, want=%v", got, want)
			}
			if got, want := tcp.SourcePort(), test.newPort; got != want {
				t.Errorf("ipv4.SourcePort()=%v, want=%v", got, want)
			}
		} else {
			if got, want := ipv4.DestinationAddress(), test.newAddr; got != want {
				t.Errorf("ipv4.DestinationAddress()=%v, want=%v", got, want)
			}
			if got, want := tcp.DestinationPort(), test.newPort; got != want {
				t.Errorf("ipv4.DestinationPort()=%v, want=%v", got, want)
			}
		}

		// Check if the checksum in the rewritten packet is correct.
		iCksum = ipv4.CalculateChecksum()
		if got, want := iCksum, uint16(0xffff); got != want {
			t.Errorf("ipv4 checksum=%x, want=%x", got, want)
		}

		tCksum = header.PseudoHeaderChecksum(header.TCPProtocolNumber,
			ipv4.SourceAddress(), ipv4.DestinationAddress())
		tCksum = header.Checksum(tcp.Payload(), tCksum)
		tCksum = tcp.CalculateChecksum(tCksum, uint16(len(th)))
		if got, want := tCksum, uint16(0xffff); got != want {
			t.Errorf("tcp checksum=%x, want=%x", got, want)
		}
	}
}

func TestRewritePacketUDPv6(t *testing.T) {
	var tests = []struct {
		packet   func() []byte
		newAddr  tcpip.Address
		newPort  uint16
		isSource bool
	}{
		{
			func() []byte {
				return udpV6Packet([]byte("payload"), &udpParams{
					srcAddr: "\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
					srcPort: 100,
					dstAddr: "\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02",
					dstPort: 200,
				})
			},
			"\x0b\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
			101,
			true,
		},
	}

	for _, test := range tests {
		b := test.packet()
		ipv6 := header.IPv6(b)
		th := b[header.IPv6MinimumSize:]
		udp := header.UDP(th)

		// Make sure the checksum in the original packet is correct.
		noUDPChecksum := false
		if udp.Checksum() == 0 {
			noUDPChecksum = true
		} else {
			tCksum := header.PseudoHeaderChecksum(header.UDPProtocolNumber,
				ipv6.SourceAddress(), ipv6.DestinationAddress())
			tCksum = header.Checksum(udp.Payload(), tCksum)
			tCksum = udp.CalculateChecksum(tCksum, uint16(len(th)))
			if got, want := tCksum, uint16(0xffff); got != want {
				t.Errorf("udp checksum=%x, want=%x", got, want)
			}
		}

		rewritePacketUDPv6(test.newAddr, test.newPort, test.isSource, b, th)

		if test.isSource {
			if got, want := ipv6.SourceAddress(), test.newAddr; got != want {
				t.Errorf("ipv6.SourceAddress()=%v, want=%v", got, want)
			}
			if got, want := udp.SourcePort(), test.newPort; got != want {
				t.Errorf("ipv6.SourcePort()=%v, want=%v", got, want)
			}
		} else {
			if got, want := ipv6.DestinationAddress(), test.newAddr; got != want {
				t.Errorf("ipv6.DestinationAddress()=%v, want=%v", got, want)
			}
			if got, want := udp.DestinationPort(), test.newPort; got != want {
				t.Errorf("ipv6.DestinationPort()=%v, want=%v", got, want)
			}
		}

		// Check if the checksum in the rewritten packet is correct.
		if noUDPChecksum {
			if got, want := udp.Checksum(), uint16(0); got != want {
				t.Errorf("udp checksum=%x, want=%x", got, want)
			}
		} else {
			tCksum := header.PseudoHeaderChecksum(header.UDPProtocolNumber,
				ipv6.SourceAddress(), ipv6.DestinationAddress())
			tCksum = header.Checksum(udp.Payload(), tCksum)
			tCksum = udp.CalculateChecksum(tCksum, uint16(len(th)))
			if got, want := tCksum, uint16(0xffff); got != want {
				t.Errorf("udp checksum=%x, want=%x", got, want)
			}
		}
	}
}

func TestRewritePacketTCPv6(t *testing.T) {
	var tests = []struct {
		packet   func() []byte
		newAddr  tcpip.Address
		newPort  uint16
		isSource bool
	}{
		{
			func() []byte {
				return tcpV6Packet([]byte("payload"), &tcpParams{
					srcAddr: "\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
					srcPort: 100,
					dstAddr: "\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02",
					dstPort: 200,
				})
			},
			"\x0b\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
			101,
			true,
		},
	}

	for _, test := range tests {
		b := test.packet()
		ipv6 := header.IPv6(b)
		th := b[header.IPv6MinimumSize:]
		tcp := header.TCP(th)

		// Make sure the checksum in the original packet is correct.
		tCksum := header.PseudoHeaderChecksum(header.TCPProtocolNumber,
			ipv6.SourceAddress(), ipv6.DestinationAddress())
		tCksum = header.Checksum(tcp.Payload(), tCksum)
		tCksum = tcp.CalculateChecksum(tCksum, uint16(len(th)))
		if got, want := tCksum, uint16(0xffff); got != want {
			t.Errorf("tcp checksum=%x, want=%x", got, want)
		}

		rewritePacketTCPv6(test.newAddr, test.newPort, test.isSource, b, th)

		if test.isSource {
			if got, want := ipv6.SourceAddress(), test.newAddr; got != want {
				t.Errorf("ipv6.SourceAddress()=%v, want=%v", got, want)
			}
			if got, want := tcp.SourcePort(), test.newPort; got != want {
				t.Errorf("ipv6.SourcePort()=%v, want=%v", got, want)
			}
		} else {
			if got, want := ipv6.DestinationAddress(), test.newAddr; got != want {
				t.Errorf("ipv6.DestinationAddress()=%v, want=%v", got, want)
			}
			if got, want := tcp.DestinationPort(), test.newPort; got != want {
				t.Errorf("ipv6.DestinationPort()=%v, want=%v", got, want)
			}
		}

		// Check if the checksum in the rewritten packet is correct.
		tCksum = header.PseudoHeaderChecksum(header.TCPProtocolNumber,
			ipv6.SourceAddress(), ipv6.DestinationAddress())
		tCksum = header.Checksum(tcp.Payload(), tCksum)
		tCksum = tcp.CalculateChecksum(tCksum, uint16(len(th)))
		if got, want := tCksum, uint16(0xffff); got != want {
			t.Errorf("tcp checksum=%x, want=%x", got, want)
		}
	}
}
