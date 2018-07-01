// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The User Datagram Protocol (UDP).

use ip::IpAddr;
use wire::udp::UdpPacket;
use wire::BufferAndRange;
use StackState;

/// The state associated with the UDP protocol.
pub struct UdpState;

/// Receive a UDP packet in an IP packet.
pub fn receive_ip_packet<A: IpAddr, B: AsMut<[u8]>>(
    state: &mut StackState, src_ip: A, dst_ip: A, mut buffer: BufferAndRange<B>,
) {
    let (packet, body_range) =
        if let Ok((packet, body_range)) = UdpPacket::parse(buffer.as_mut(), src_ip, dst_ip) {
            (packet, body_range)
        } else {
            // TODO(joshlf): Do something with ICMP here?
            return;
        };
    unimplemented!()
}
