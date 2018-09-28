// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Address Resolution Protocol (ARP).

use std::collections::HashMap;
use std::hash::Hash;

use crate::device::ethernet::EthernetArpDevice;
use crate::wire::{arp::{ArpPacket, HType, PType},
                  BufferAndRange, SerializationCallback};
use crate::{Context, EventDispatcher};

/// The type of an ARP operation.
#[derive(Debug, Eq, PartialEq)]
#[allow(missing_docs)]
#[repr(u16)]
pub enum ArpOp {
    Request = ArpOp::REQUEST,
    Response = ArpOp::RESPONSE,
}

impl ArpOp {
    const REQUEST: u16 = 0x0001;
    const RESPONSE: u16 = 0x0002;

    /// Construct an `ArpOp` from a `u16`.
    ///
    /// `from_u16` returns the `ArpOp` with the numerical value `u`, or `None`
    /// if the value is unrecognized.
    pub fn from_u16(u: u16) -> Option<ArpOp> {
        match u {
            Self::REQUEST => Some(ArpOp::Request),
            Self::RESPONSE => Some(ArpOp::Response),
            _ => None,
        }
    }
}

/// An ARP hardware protocol.
#[derive(Debug, PartialEq)]
#[allow(missing_docs)]
#[repr(u16)]
pub enum ArpHardwareType {
    Ethernet = ArpHardwareType::ETHERNET,
}

impl ArpHardwareType {
    const ETHERNET: u16 = 0x0001;

    /// Construct an `ArpHardwareType` from a `u16`.
    ///
    /// `from_u16` returns the `ArpHardwareType` with the numerical value `u`,
    /// or `None` if the value is unrecognized.
    pub fn from_u16(u: u16) -> Option<ArpHardwareType> {
        match u {
            Self::ETHERNET => Some(ArpHardwareType::Ethernet),
            _ => None,
        }
    }
}

/// A device layer protocol which can support ARP.
///
/// An `ArpDevice<P>` is a device layer protocol which can support ARP with the
/// network protocol `P` (e.g., IPv4, IPv6, etc).
pub trait ArpDevice<P: PType + Eq + Hash>: Sized {
    /// The hardware address type used by this protocol.
    type HardwareAddr: HType;

    /// The broadcast address.
    const BROADCAST: Self::HardwareAddr;

    /// Send an ARP packet in a device layer frame.
    ///
    /// `send_arp_frame` accepts a device ID, a destination hardware address,
    /// and a callback. It computes the routing information and invokes the
    /// callback with the number of prefix bytes required by all encapsulating
    /// headers, and the minimum size of the body plus padding. The callback is
    /// expected to return a byte buffer and a range which corresponds to the
    /// desired body. The portion of the buffer beyond the end of the body range
    /// will be treated as padding. The total number of bytes in the body and
    /// the post-body padding must not be smaller than the minimum size passed
    /// to the callback.
    ///
    /// For more details on the callback, see the
    /// [`crate::wire::SerializationCallback`] documentation.
    ///
    /// # Panics
    ///
    /// `send_arp_frame` panics if the buffer returned from `get_buffer` does
    /// not have sufficient space preceding the body for all encapsulating
    /// headers or does not have enough body plus padding bytes to satisfy the
    /// requirement passed to the callback.
    fn send_arp_frame<D: EventDispatcher, B, F>(
        ctx: &mut Context<D>, device_id: u64, dst: Self::HardwareAddr, get_buffer: F,
    ) where
        B: AsRef<[u8]> + AsMut<[u8]>,
        F: SerializationCallback<B>;

    /// Get a mutable reference to a device's ARP state.
    fn get_arp_state<D: EventDispatcher>(
        ctx: &mut Context<D>, device_id: u64,
    ) -> &mut ArpState<P, Self>;

    /// Get the protocol address of this interface.
    fn get_protocol_addr<D: EventDispatcher>(ctx: &mut Context<D>, device_id: u64) -> Option<P>;
}

/// Receive an ARP packet from a device.
///
/// The protocol and hardware types (`P` and `D::HardwareAddr` respectively)
/// must be set statically. Unless there is only one valid pair of protocol and
/// hardware types in a given context, it is the caller's responsibility to call
/// `peek_arp_types` in order to determine which types to use in calling this
/// function.
pub fn receive_arp_packet<
    D: EventDispatcher,
    P: PType + Eq + Hash,
    AD: ArpDevice<P>,
    B: AsRef<[u8]> + AsMut<[u8]>,
>(
    ctx: &mut Context<D>, device_id: u64, src_addr: AD::HardwareAddr, dst_addr: AD::HardwareAddr,
    mut buffer: BufferAndRange<B>,
) {
    // TODO(wesleyac) Add support for gratuitous ARP and probe/announce.
    let packet = if let Ok(packet) = ArpPacket::<_, AD::HardwareAddr, P>::parse(buffer.as_mut()) {
        let addressed_to_me =
            Some(packet.target_protocol_address()) == AD::get_protocol_addr(ctx, device_id);
        let table = &mut AD::get_arp_state(ctx, device_id).table;
        // The following logic is equivalent to the "Packet Reception" section of RFC 826.
        //
        // We statically know that the hardware type and protocol type are correct, so we do not
        // need to have additional code to check that. The remainder of the algorithm is:
        //
        // Merge_flag := false
        // If the pair <protocol type, sender protocol address> is
        //     already in my translation table, update the sender
        //     hardware address field of the entry with the new
        //     information in the packet and set Merge_flag to true.
        // ?Am I the target protocol address?
        // Yes:
        //   If Merge_flag is false, add the triplet <protocol type,
        //       sender protocol address, sender hardware address> to
        //       the translation table.
        //   ?Is the opcode ares_op$REQUEST?  (NOW look at the opcode!!)
        //   Yes:
        //     Swap hardware and protocol fields, putting the local
        //         hardware and protocol addresses in the sender fields.
        //     Set the ar$op field to ares_op$REPLY
        //     Send the packet to the (new) target hardware address on
        //         the same hardware on which the request was received.
        //
        // This can be summed up as follows:
        //
        // +----------+---------------+---------------+-----------------------------+
        // | opcode   | Am I the TPA? | SPA in table? | action                      |
        // +----------+---------------+---------------+-----------------------------+
        // | REQUEST  | yes           | yes           | Update table, Send response |
        // | REQUEST  | yes           | no            | Update table, Send response |
        // | REQUEST  | no            | yes           | Update table                |
        // | REQUEST  | no            | no            | NOP                         |
        // | RESPONSE | yes           | yes           | Update table                |
        // | RESPONSE | yes           | no            | Update table                |
        // | RESPONSE | no            | yes           | Update table                |
        // | RESPONSE | no            | no            | NOP                         |
        // +----------+---------------+---------------+-----------------------------+
        //
        // Given that the semantics of ArpTable is that inserting and updating an entry are the
        // same, this can be implemented with two if statements (one to update the table, and one
        // to send a response).

        if addressed_to_me || table.lookup(packet.sender_protocol_address()).is_some() {
            table.insert(
                packet.sender_protocol_address(),
                packet.sender_hardware_address(),
            );
        }
        if addressed_to_me && packet.operation() == ArpOp::Request {
            log_unimplemented!((), "device::arp::receive_arp_frame: Handling ARP requests not implemented");
        }
    } else {
        // TODO(joshlf): Do something else here?
        return;
    };
}

/// Look up the hardware address for a network protocol address.
pub fn lookup<D: EventDispatcher, P: PType + Eq + Hash, AD: ArpDevice<P>>(
    ctx: &mut Context<D>, device_id: u64, local_addr: AD::HardwareAddr, lookup: P,
) -> Option<AD::HardwareAddr> {
    // TODO(joshlf): Figure out what to do if a frame can't be sent right now
    // because it needs to wait for an ARP reply. Where do we put those frames?
    // How do we associate them with the right ARP reply? How do we retreive
    // them when we get that ARP reply? How do we time out so we don't hold onto
    // a stale frame forever?
    AD::get_arp_state(ctx, device_id)
        .table
        .lookup(lookup)
        .cloned()
}

/// The state associated with an instance of the Address Resolution Protocol
/// (ARP).
///
/// Each device will contain an `ArpState` object for each of the network
/// protocols that it supports.
pub struct ArpState<P: PType + Hash + Eq, D: ArpDevice<P>> {
    // NOTE(joshlf): Taking an ArpDevice type parameter is technically
    // unnecessary here; we could instead just be parametric on a hardware type
    // and a network protocol type. However, doing it this way ensure that
    // device layer code doesn't accidentally invoke receive_arp_packet with
    // different ArpDevice implementations in different places (this would fail
    // to compile because the get_arp_state method on ArpDevice returns an
    // ArpState<_, Self>, which requires that the ArpDevice implementation
    // matches the type of the ArpState stored in that device's state).
    table: ArpTable<D::HardwareAddr, P>,
}

impl<P: PType + Hash + Eq, D: ArpDevice<P>> Default for ArpState<P, D> {
    fn default() -> Self {
        ArpState {
            table: ArpTable::default(),
        }
    }
}

struct ArpTable<H, P: Hash + Eq> {
    table: HashMap<P, ArpValue<H>>,
}

#[derive(Debug, Eq, PartialEq)] // for testing
enum ArpValue<H> {
    Known(H),
    Waiting,
}

impl<H, P: Hash + Eq> ArpTable<H, P> {
    fn insert(&mut self, net: P, hw: H) {
        self.table.insert(net, ArpValue::Known(hw));
    }

    // TODO(wesleyac): figure out how to send arp requests on cache misses
    fn lookup(&self, addr: P) -> Option<&H> {
        match self.table.get(&addr) {
            Some(ArpValue::Known(x)) => Some(x),
            _ => None,
        }
    }
}

impl<H, P: Hash + Eq> Default for ArpTable<H, P> {
    fn default() -> Self {
        ArpTable {
            table: HashMap::default(),
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::device::arp::*;
    use crate::device::ethernet::{set_ip_addr, EtherType, Mac};
    use crate::device::DeviceId;
    use crate::device::DeviceLayerEventDispatcher;
    use crate::ip::{Ipv4Addr, Subnet};
    use crate::testutil::DummyEventDispatcher;
    use crate::transport::TransportLayerEventDispatcher;
    use crate::wire::arp::peek_arp_types;
    use crate::wire::ethernet::EthernetFrame;
    use crate::StackState;

    const TEST_SENDER_IPV4: Ipv4Addr = Ipv4Addr::new([1, 2, 3, 4]);
    const TEST_TARGET_IPV4: Ipv4Addr = Ipv4Addr::new([5, 6, 7, 8]);
    const TEST_SENDER_MAC: Mac = Mac::new([0, 1, 2, 3, 4, 5]);
    const TEST_TARGET_MAC: Mac = Mac::new([6, 7, 8, 9, 10, 11]);

    #[test]
    fn test_recv_arp_response() {
        let mut state = StackState::default();
        let dev_id = state
            .device
            .add_ethernet_device(Mac::new([4, 4, 4, 4, 4, 4]));
        let dispatcher = DummyEventDispatcher {};
        let mut ctx: Context<DummyEventDispatcher> = Context::new(state, dispatcher);
        set_ip_addr(
            &mut ctx,
            dev_id.id,
            TEST_TARGET_IPV4,
            Subnet::new(TEST_TARGET_IPV4, 24),
        );

        let mut buf = [0; 28];
        {
            ArpPacket::serialize(
                &mut buf[..],
                ArpOp::Request,
                TEST_SENDER_MAC,
                TEST_SENDER_IPV4,
                TEST_TARGET_MAC,
                TEST_TARGET_IPV4,
            );
        }
        let (hw, proto) = peek_arp_types(&buf[..]).unwrap();
        assert_eq!(hw, ArpHardwareType::Ethernet);
        assert_eq!(proto, EtherType::Ipv4);
        let arp = BufferAndRange::new(&mut buf[..], ..);

        receive_arp_packet::<DummyEventDispatcher, Ipv4Addr, EthernetArpDevice, &mut [u8]>(
            &mut ctx,
            0,
            TEST_SENDER_MAC,
            TEST_TARGET_MAC,
            arp,
        );

        assert_eq!(
            lookup::<DummyEventDispatcher, Ipv4Addr, EthernetArpDevice>(
                &mut ctx,
                0,
                TEST_TARGET_MAC,
                TEST_SENDER_IPV4
            ).unwrap(),
            TEST_SENDER_MAC
        );
    }

    #[test]
    fn test_arp_table() {
        let mut t: ArpTable<Mac, Ipv4Addr> = ArpTable::default();
        assert_eq!(t.lookup(Ipv4Addr::new([10, 0, 0, 1])), None);
        t.insert(Ipv4Addr::new([10, 0, 0, 1]), Mac::new([1, 2, 3, 4, 5, 6]));
        assert_eq!(
            *t.lookup(Ipv4Addr::new([10, 0, 0, 1])).unwrap(),
            Mac::new([1, 2, 3, 4, 5, 6])
        );
        assert_eq!(t.lookup(Ipv4Addr::new([10, 0, 0, 2])), None);
    }
}
