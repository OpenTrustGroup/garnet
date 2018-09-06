// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

// Types from <fdio/socket.h>.
// These were extracted from cgo by building the program:
//
//	package typesextract
//
//	// #include <arpa/inet.h>
//	// #include <netdb.h>
//	// #include <netinet/in.h>
//	// #include <netinet/tcp.h>
//	// #include <sys/ioctl.h>
//	// #include <sys/socket.h>
//	// #include <sys/types.h>
//	// #include <fdio/socket.h>
//	import "C"
//
//	func F() {
//		_ = C.struct_addrinfo{}
//		_ = C.struct_sockaddr_in{}
//		_ = C.struct_sockaddr_in6{}
//		_ = C.struct_mxrio_sockaddr_reply{}
//		_ = C.struct_mxrio_sockopt_req_reply{}
//		_ = C.struct_fdio_socket_msg{}
//		_ = C.AF_INET
//		_ = C.AF_INET6
//		_ = C.SOL_SOCKET
//		_ = C.SOL_TCP
//
//		_ = C.SO_REUSEADDR
//		_ = C.SO_TYPE
//		_ = C.SO_ERROR
//		_ = C.SO_DONTROUTE
//		_ = C.SO_BROADCAST
//		_ = C.SO_SNDBUF
//		_ = C.SO_RCVBUF
//		_ = C.SO_KEEPALIVE
//		_ = C.SO_NO_CHECK
//		_ = C.SO_PRIORITY
//		_ = C.SO_LINGER
//		_ = C.SO_BSDCOMPAT
//		_ = C.SO_REUSEPORT
//		_ = C.SO_PASSCRED
//		_ = C.SO_PEERCRED
//		_ = C.SO_RCVLOWAT
//		_ = C.SO_SNDLOWAT
//		_ = C.SO_RCVTIMEO
//		_ = C.SO_SNDTIMEO
//		_ = C.SO_ACCEPTCONN
//		_ = C.SO_SNDBUFFORCE
//		_ = C.SO_RCVBUFFORCE
//		_ = C.SO_PROTOCOL
//		_ = C.SO_DOMAIN
//
//		_ = C.TCP_NODELAY
//		_ = C.TCP_MAXSEG
//		_ = C.TCP_CORK
//		_ = C.TCP_KEEPIDLE
//		_ = C.TCP_KEEPINTVL
//		_ = C.TCP_KEEPCNT
//		_ = C.TCP_SYNCNT
//		_ = C.TCP_LINGER2
//		_ = C.TCP_DEFER_ACCEPT
//		_ = C.TCP_WINDOW_CLAMP
//		_ = C.TCP_INFO
//		_ = C.TCP_QUICKACK
//	}
//
// Then the names were cleaned up manually, with commands like:
//	s/_t$//
//	s/_Ctype_struct_/c_/
//	s/_Ctype_/c_/
//	...
//
// Two big changes are c_in_port and c_in_addr, which for historcical
// UNIX reasons have terrible type representations, so we treat them
// as byte arrays.
//
// This will probably all be FIDL one day.

// TODO: move some subset of these constants to syscall package
const O_RDWR = 02
const O_DIRECTORY = 02000000
const SOCK_STREAM = 1
const SOCK_DGRAM = 2

const IPPROTO_IP = 0
const IPPROTO_ICMP = 1
const IPPROTO_TCP = 6
const IPPROTO_UDP = 17
const IPPROTO_ICMPV6 = 58

const AF_UNSPEC = 0
const AF_INET = 2
const AF_INET6 = 10

const SOL_IP = 0
const SOL_SOCKET = 0x1
const SOL_TCP = 0x6

const IP_TOS = 1
const IP_TTL = 2
const IP_MULTICAST_IF = 32
const IP_MULTICAST_TTL = 33
const IP_MULTICAST_LOOP = 34
const IP_ADD_MEMBERSHIP = 35
const IP_DROP_MEMBERSHIP = 36

const SO_ACCEPTCONN = 0x1e
const SO_BROADCAST = 0x6
const SO_BSDCOMPAT = 0xe
const SO_DEBUG = 0x1
const SO_DOMAIN = 0x27
const SO_DONTROUTE = 0x5
const SO_ERROR = 0x4
const SO_KEEPALIVE = 0x9
const SO_LINGER = 0xd
const SO_NO_CHECK = 0xb
const SO_PASSCRED = 0x10
const SO_PEERCRED = 0x11
const SO_PRIORITY = 0xc
const SO_PROTOCOL = 0x26
const SO_RCVBUF = 0x8
const SO_RCVBUFFORCE = 0x21
const SO_RCVLOWAT = 0x12
const SO_RCVTIMEO = 0x14
const SO_REUSEADDR = 0x2
const SO_REUSEPORT = 0xf
const SO_SNDBUF = 0x7
const SO_SNDBUFFORCE = 0x20
const SO_SNDLOWAT = 0x13
const SO_SNDTIMEO = 0x15
const SO_TYPE = 0x3

const TCP_NODELAY = 1
const TCP_MAXSEG = 2
const TCP_CORK = 3
const TCP_KEEPIDLE = 4
const TCP_KEEPINTVL = 5
const TCP_KEEPCNT = 6
const TCP_SYNCNT = 7
const TCP_LINGER2 = 8
const TCP_DEFER_ACCEPT = 9
const TCP_WINDOW_CLAMP = 10
const TCP_INFO = 11
const TCP_QUICKACK = 12

const EAI_BADFLAGS = -1
const EAI_NONAME = -2
const EAI_AGAIN = -3
const EAI_FAIL = -4
const EAI_FAMILY = -6
const EAI_SOCKTYPE = -7
const EAI_SERVICE = -8
const EAI_MEMORY = -10
const EAI_SYSTEM = -11
const EAI_OVERFLOW = -12

const NETC_IFF_UP = 1

type c_socklen uint32
type c_in_port [2]byte // uint16 in C, but stored in network order
type c_sa_family uint16
type c_in_addr [4]byte // uint32 in C, but stored in network order
type c_ulong uint64

type c_mxrio_sockaddr_reply struct {
	addr c_sockaddr_storage
	len  c_socklen
	_    [4]byte
}

type c_mxrio_sockopt_req_reply struct {
	level   int32
	optname int32
	optval  [128]uint8
	optlen  c_socklen
}

type c_mxrio_sockopt_tcp_info struct {
	state              uint8
	ca_state           uint8
	retransmits        uint8
	probes             uint8
	backoff            uint8
	options            uint8
	snd_and_rcv_wscale uint8

	rto     uint32
	ato     uint32
	snd_mss uint32
	rcv_mss uint32

	unacked uint32
	sacked  uint32
	lost    uint32
	retrans uint32
	fackets uint32

	// Times
	last_data_sent uint32
	last_ack_sent  uint32
	last_data_recv uint32
	last_ack_recv  uint32

	// Metrics
	pmtu         uint32
	rcv_ssthresh uint32
	rtt          uint32
	rttvar       uint32
	snd_ssthresh uint32
	snd_cwnd     uint32
	advmss       uint32
	reordering   uint32

	rcv_rtt   uint32
	rcv_space uint32

	total_retrans uint32

	pacing_rate     uint64
	max_pacing_rate uint64
	bytes_acked     uint64
	bytes_received  uint64

	segs_out uint32
	segs_in  uint32
}

type c_sockaddr struct {
	sa_family c_sa_family
	sa_data   [14]uint8
}

type c_sockaddr_in struct {
	sin_family c_sa_family
	sin_port   c_in_port // network order
	sin_addr   c_in_addr
	sin_zero   [8]uint8
}

type c_in6_addr [16]byte

type c_sockaddr_in6 struct {
	sin6_family   c_sa_family
	sin6_port     c_in_port // network order
	sin6_flowinfo uint32
	sin6_addr     c_in6_addr
	sin6_scope_id uint32
}

type c_sockaddr_storage struct {
	ss_family    c_sa_family
	_            [6]byte
	__ss_align   c_ulong
	__ss_padding [112]uint8
}

type c_fdio_socket_msg_hdr struct {
	addr    c_sockaddr_storage
	addrlen c_socklen
	flags   int32
}

type c_fdio_socket_msg struct {
	hdr  c_fdio_socket_msg_hdr
	data [1]uint8
	_    [7]byte
}

type c_ip_mreq struct {
	imr_multiaddr c_in_addr
	imr_interface c_in_addr
}

type c_ip_mreqn struct {
	imr_multiaddr c_in_addr
	imr_address   c_in_addr
	imr_ifindex   int32
}

type c_netc_if_info struct {
	name       [16]byte
	addr       c_sockaddr_storage
	netmask    c_sockaddr_storage
	broadaddr  c_sockaddr_storage
	flags      uint32
	index      uint16
	hwaddr_len uint16
	hwaddr     [8]uint8
}

type c_netc_get_if_info struct {
	n_info uint32
	info   [16]c_netc_if_info
}
