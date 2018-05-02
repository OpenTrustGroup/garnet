// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Ad-hoc unsafe encoding/decoding of C data types used by the
// protocol libc talks to netstack.

package main

import (
	"fmt"
	"log"
	"syscall/zx"
	"syscall/zx/mxerror"
	"syscall/zx/zxsocket"
	"unsafe"

	"github.com/google/netstack/tcpip"
)

const (
	c_sockaddr_in_len          = int(unsafe.Sizeof(c_sockaddr_in{}))
	c_sockaddr_in6_len         = int(unsafe.Sizeof(c_sockaddr_in6{}))
	c_sockaddr_storage_len     = int(unsafe.Sizeof(c_sockaddr_storage{}))
	c_mxrio_sockaddr_reply_len = int(unsafe.Sizeof(c_mxrio_sockaddr_reply{}))
	c_fdio_socket_msg_hdr_len  = int(unsafe.Sizeof(c_fdio_socket_msg_hdr{}))
)

func init() {
	if c_sockaddr_storage_len < c_sockaddr_in_len {
		panic("c_sockaddr_storage cannot hold c_sockaddr_in")
	}
	if c_sockaddr_storage_len < c_sockaddr_in6_len {
		panic("c_sockaddr_storage cannot hold c_sockaddr_in6")
	}
}

func (v *c_mxrio_sockopt_req_reply) Decode(data []byte) error {
	if uintptr(len(data)) < unsafe.Sizeof(c_mxrio_sockopt_req_reply{}) {
		return fmt.Errorf("netstack: short c_mxrio_sockopt_req_reply: %d", len(data))
	}
	req := (*c_mxrio_sockopt_req_reply)(unsafe.Pointer(&data[0]))
	*v = *req
	return nil
}

func (v *c_mxrio_sockopt_req_reply) Encode(msg *zxsocket.Msg) {
	r := (*c_mxrio_sockopt_req_reply)(unsafe.Pointer(&msg.Data[0]))
	*r = *v
	msg.Datalen = uint32(unsafe.Sizeof(*v))
}

func (v *c_mxrio_gai_req) Decode(msg *zxsocket.Msg) error {
	data := msg.Data[:msg.Datalen]
	if uintptr(len(data)) < unsafe.Sizeof(c_mxrio_gai_req{}) {
		return fmt.Errorf("netstack: short c_mxrio_gai_req: %d", len(data))
	}
	req := (*c_mxrio_gai_req)(unsafe.Pointer(&data[0]))
	*v = *req
	return nil
}

func (v *c_mxrio_sockopt_tcp_info) Encode(out *c_mxrio_sockopt_req_reply) error {
	r := (*c_mxrio_sockopt_tcp_info)(unsafe.Pointer(&out.optval[0]))
	*r = *v
	out.optlen = c_socklen(unsafe.Sizeof(*v))
	return nil
}

func (v *c_mxrio_gai_reply) Encode(msg *zxsocket.Msg) {
	r := (*c_mxrio_gai_reply)(unsafe.Pointer(&msg.Data[0]))
	*r = *v
	msg.Datalen = uint32(unsafe.Sizeof(*v))
}

func (v *c_mxrio_sockaddr_reply) Encode(msg *zxsocket.Msg) {
	r := (*c_mxrio_sockaddr_reply)(unsafe.Pointer(&msg.Data[0]))
	*r = *v
	msg.Datalen = uint32(unsafe.Sizeof(*v))
}

func (v *c_ip_mreq) Decode(data []byte) error {
	if uintptr(len(data)) < unsafe.Sizeof(c_ip_mreq{}) {
		return fmt.Errorf("netstack: short c_ip_mreq: %d", len(data))
	}
	req := (*c_ip_mreq)(unsafe.Pointer(&data[0]))
	*v = *req
	return nil
}

func (v *c_netc_get_if_info) Encode(msg *zxsocket.Msg) {
	r := (*c_netc_get_if_info)(unsafe.Pointer(&msg.Data[0]))
	*r = *v
	msg.Datalen = uint32(unsafe.Sizeof(*v))
}

func (v *c_netc_if_info) Encode(msg *zxsocket.Msg) {
	r := (*c_netc_if_info)(unsafe.Pointer(&msg.Data[0]))
	*r = *v
	msg.Datalen = uint32(unsafe.Sizeof(*v))
}

// TODO: make these methods on c_sockaddr_storage
func writeSockaddrStorage4(dst *c_sockaddr_storage, src *c_sockaddr_in) c_socklen {
	srcb := (*[unsafe.Sizeof(*src)]byte)(unsafe.Pointer(src))[:]
	dstb := (*[unsafe.Sizeof(*dst)]byte)(unsafe.Pointer(dst))[:]
	return c_socklen(copy(dstb, srcb))
}
func writeSockaddrStorage6(dst *c_sockaddr_storage, src *c_sockaddr_in6) c_socklen {
	srcb := (*[unsafe.Sizeof(*src)]byte)(unsafe.Pointer(src))[:]
	dstb := (*[unsafe.Sizeof(*dst)]byte)(unsafe.Pointer(dst))[:]
	return c_socklen(copy(dstb, srcb))
}
func writeSockaddrStorage(dst *c_sockaddr_storage, a tcpip.FullAddress) (c_socklen, error) {
	switch len(a.Addr) {
	case 0, 4:
		sockaddr := c_sockaddr_in{sin_family: AF_INET}
		sockaddr.sin_port.setPort(a.Port)
		copy(sockaddr.sin_addr[:], a.Addr)
		return writeSockaddrStorage4(dst, &sockaddr), nil
	case 16:
		sockaddr := c_sockaddr_in6{sin6_family: AF_INET6}
		sockaddr.sin6_port.setPort(a.Port)
		copy(sockaddr.sin6_addr[:], a.Addr)
		return writeSockaddrStorage6(dst, &sockaddr), nil
	}
	return 0, mxerror.Errorf(zx.ErrInvalidArgs, "write sockaddr: bad address len %d", len(a.Addr))
}

func (v c_in_port) port() uint16 {
	return uint16(v[0])<<8 | uint16(v[1])
}

func (v *c_in_port) setPort(port uint16) {
	v[0] = uint8(port >> 8)
	v[1] = uint8(port)
}

func readSockaddrIn(data []byte) (*tcpip.FullAddress, error) {
	// TODO: recast in terms of c_sockaddr_storage
	// TODO: split out the not-unsafe parts into socket_conv.go.
	family := uint16(data[0]) | uint16(data[1])<<8
	if debug2 {
		log.Printf("readSockaddrIn: family=%d", family)
	}
	switch family {
	case AF_INET:
		if len(data) < int(unsafe.Sizeof(c_sockaddr_in{})) {
			return nil, mxerror.Errorf(zx.ErrInvalidArgs, "reading c_sockaddr_in: len(data)=%d too small", len(data))
		}
		v := (*c_sockaddr_in)(unsafe.Pointer(&data[0]))
		addr := &tcpip.FullAddress{
			Port: uint16(data[3]) | uint16(data[2])<<8,
		}
		// INADDR_ANY is represented as tcpip.Address("").
		if !isZeros(v.sin_addr[:]) {
			addr.Addr = tcpip.Address(v.sin_addr[:])
		}
		if debug2 {
			log.Printf("readSockaddrIn: addr=%v", addr)
		}
		return addr, nil
	case AF_INET6:
		if len(data) < int(unsafe.Sizeof(c_sockaddr_in6{})) {
			return nil, mxerror.Errorf(zx.ErrInvalidArgs, "reading c_sockaddr_in6: len(data)=%d too small", len(data))
		}
		v := (*c_sockaddr_in6)(unsafe.Pointer(&data[0]))
		addr := &tcpip.FullAddress{
			Port: uint16(data[3]) | uint16(data[2])<<8,
		}
		if !isZeros(v.sin6_addr[:]) {
			addr.Addr = tcpip.Address(v.sin6_addr[:])
		}
		if debug2 {
			log.Printf("readSockaddrIn: addr=%v", addr)
		}
		return addr, nil
	default:
		return nil, mxerror.Errorf(zx.ErrInvalidArgs, "reading c_sockaddr: unknown family: %d", family)
	}
}

func readSocketMsgHdr(data []byte) (*tcpip.FullAddress, error) {
	if len(data) < c_fdio_socket_msg_hdr_len {
		return nil, mxerror.Errorf(zx.ErrInvalidArgs, "reading socket msg header: too short: %d", len(data))
	}
	hdr := (*c_fdio_socket_msg_hdr)(unsafe.Pointer(&data[0]))
	if hdr.addrlen == 0 {
		return nil, nil
	}
	return readSockaddrIn(data) // first field of c_fdio_socket_msg_hdr is c_sockaddr_storage
}

func writeSocketMsgHdr(data []byte, addr tcpip.FullAddress) error {
	if len(data) < c_fdio_socket_msg_hdr_len {
		return mxerror.Errorf(zx.ErrInvalidArgs, "writing socket msg header: too short: %d", len(data))
	}
	hdr := (*c_fdio_socket_msg_hdr)(unsafe.Pointer(&data[0]))
	l, err := writeSockaddrStorage(&hdr.addr, addr)
	hdr.addrlen = l
	hdr.flags = 0
	return err
}
