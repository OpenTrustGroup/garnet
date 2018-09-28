// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Fuchsia Ethernet client

#![feature(arbitrary_self_types, futures_api, pin)]
#![deny(warnings)]
#![deny(missing_docs)]

use bitflags::{bitflags, __bitflags, __impl_bitflags};
use fuchsia_async as fasync;
use futures::{prelude::*, ready, try_ready};
use fuchsia_zircon::{self as zx, AsHandleRef, HandleBased};

use std::fs::File;
use std::marker::Unpin;
use std::pin::PinMut;
use std::sync::{Arc, Mutex};

mod buffer;
mod sys;

/// Default buffer size communicating with Ethernet devices.
///
/// It is the smallest power of 2 greater than the Ethernet MTU of 1500.
pub const DEFAULT_BUFFER_SIZE: usize = 2048;

bitflags! {
    /// Features supported by an Ethernet device.
    #[repr(transparent)]
    #[derive(Default)]
    pub struct EthernetFeatures: u32 {
        /// The Ethernet device is a wireless device.
        const WLAN = sys::ETH_FEATURE_WLAN;
        /// The Ethernet device does not represent a hardware device.
        const SYNTHETIC = sys::ETH_FEATURE_SYNTH;
        /// The Ethernet device is a loopback device.
        ///
        /// This bit should not be set outside of network stacks.
        const LOOPBACK = sys::ETH_FEATURE_LOOPBACK;
    }
}

/// Information retrieved about an Ethernet device.
#[derive(Copy, Clone, Debug)]
pub struct EthernetInfo {
    /// The features supported by the device.
    pub features: EthernetFeatures,
    /// The maximum transmission unit (MTU) of the device.
    pub mtu: u32,
    /// The MAC address of the device.
    pub mac: [u8; 6],
}

bitflags! {
    /// Status flags for an Ethernet device.
    #[repr(transparent)]
    #[derive(Default)]
    pub struct EthernetStatus: u32 {
        /// The Ethernet device is online, meaning its physical link is up.
        const ONLINE = sys::ETH_STATUS_ONLINE;
    }
}

bitflags! {
    /// Status flags describing the result of queueing a packet to an Ethernet device.
    #[repr(transparent)]
    #[derive(Default)]
    pub struct EthernetQueueFlags: u16 {
        /// The packet was received correctly.
        const RX_OK = sys::ETH_FIFO_RX_OK;
        /// The packet was transmitted correctly.
        const TX_OK = sys::ETH_FIFO_TX_OK;
        /// The packet was out of the bounds of the memory shared with the Ethernet device driver.
        const INVALID = sys::ETH_FIFO_INVALID;
        /// The received packet was sent by this host.
        ///
        /// This bit is only set after `tx_listen_start` is called.
        const TX_ECHO = sys::ETH_FIFO_RX_TX;
    }
}

/// An Ethernet client that communicates with a device driver to send and receive packets.
#[derive(Debug)]
pub struct Client {
    inner: Arc<ClientInner>,
}

impl Client {
    /// Create a new Ethernet client for the device represented by the `dev` file.
    ///
    /// The `buf` is used to share memory between this process and the device driver, and must
    /// remain valid as long as this client is valid. The `name` is used by the driver for debug
    /// logs.
    ///
    /// TODO(tkilbourn): handle the buffer size better. How does the user of this crate know what
    /// to pass, before the device is opened?
    pub fn new(
        dev: File,
        buf: zx::Vmo,
        buf_size: usize,
        name: &str,
    ) -> Result<Self, zx::Status> {
        sys::set_client_name(&dev, name)?;
        let fifos = sys::get_fifos(&dev)?;
        {
            let buf = zx::Vmo::from(buf.as_handle_ref().duplicate(zx::Rights::SAME_RIGHTS)?);
            sys::set_iobuf(&dev, buf)?;
        }
        let pool = Mutex::new(buffer::BufferPool::new(buf, buf_size)?);
        Ok(Client {
            inner: Arc::new(ClientInner::new(dev, pool, fifos)?),
        })
    }

    /// Get a stream of events from the Ethernet device.
    ///
    /// This is a default implementation using the various "poll" methods on this `Client`; more
    /// sophisticated uses should use those methods directly to implement a `Future` or `Stream`.
    pub fn get_stream(&self) -> EventStream {
        EventStream {
            inner: Arc::clone(&self.inner),
        }
    }

    /// Retrieve information about the Ethernet device.
    pub fn info(&self) -> Result<EthernetInfo, zx::Status> {
        let info = sys::get_info(&self.inner.dev)?;
        let mut features = EthernetFeatures::default();
        if info.features & sys::ETH_FEATURE_WLAN != 0 {
            features |= EthernetFeatures::WLAN;
        }
        if info.features & sys::ETH_FEATURE_SYNTH != 0 {
            features |= EthernetFeatures::SYNTHETIC;
        }
        if info.features & sys::ETH_FEATURE_LOOPBACK != 0 {
            features |= EthernetFeatures::LOOPBACK;
        }
        Ok(EthernetInfo {
            features,
            mtu: info.mtu,
            mac: info.mac,
        })
    }

    /// Start transferring packets.
    ///
    /// Before this is called, no packets will be tranferred.
    pub fn start(&self) -> Result<(), zx::Status> {
        sys::start(&self.inner.dev)
    }

    /// Stop transferring packets.
    ///
    /// After this is called, no packets will be transferred.
    pub fn stop(&self) -> Result<(), zx::Status> {
        sys::stop(&self.inner.dev)
    }

    /// Start receiving all packets transmitted by this host.
    ///
    /// Such packets will have the `EthernetQueueFlags::TX_ECHO` bit set.
    pub fn tx_listen_start(&self) -> Result<(), zx::Status> {
        sys::tx_listen_start(&self.inner.dev)
    }

    /// Stop receiving all packets transmitted by this host.
    pub fn tx_listen_stop(&self) -> Result<(), zx::Status> {
        sys::tx_listen_stop(&self.inner.dev)
    }

    /// Get the status of the Ethernet device.
    pub fn get_status(&self) -> Result<EthernetStatus, zx::Status> {
        Ok(EthernetStatus::from_bits_truncate(sys::get_status(
            &self.inner.dev,
        )?))
    }

    /// Send a buffer with the Ethernet device.
    ///
    /// TODO(tkilbourn): indicate to the caller whether the send failed due to lack of buffers.
    pub fn send(&mut self, buf: &[u8]) {
        let mut guard = self.inner.pool.lock().unwrap();
        match guard.alloc_tx_buffer() {
            None => (),
            Some(mut t) => {
                t.write(buf);
                self.inner.tx_pending.lock().unwrap().push(t.entry());
            }
        }
    }

    /// Poll the Ethernet client to see if the status has changed.
    pub fn poll_status(&self, cx: &mut task::Context) -> Poll<Result<zx::Signals, zx::Status>> {
        self.inner.poll_status(cx)
    }

    /// Poll the Ethernet client to queue any pending packets for transmit.
    pub fn poll_queue_tx(&self, cx: &mut task::Context) -> Poll<Result<usize, zx::Status>> {
        self.inner.poll_queue_tx(cx)
    }

    /// Poll the Ethernet client to complete any attempted packet transmissions.
    pub fn poll_complete_tx(&self, cx: &mut task::Context) -> Poll<Result<EthernetQueueFlags, zx::Status>> {
        self.inner.poll_complete_tx(cx)
    }

    /// Poll the Ethernet client to queue a buffer for receiving packets from the Ethernet device.
    pub fn poll_queue_rx(&self, cx: &mut task::Context) -> Poll<Result<(), zx::Status>> {
        self.inner.poll_queue_rx(cx)
    }

    /// Poll the Ethernet client to receive a packet from the Ethernet device.
    pub fn poll_complete_rx(&self, cx: &mut task::Context) -> Poll<Result<buffer::RxBuffer, zx::Status>> {
        self.inner.poll_complete_rx(cx)
    }
}

/// A stream of events from the Ethernet device.
pub struct EventStream {
    inner: Arc<ClientInner>,
}

impl Unpin for EventStream {}

/// An event from the Ethernet device.
#[derive(Debug)]
pub enum Event {
    /// The Ethernet device indicated that its status has changed.
    ///
    /// The `get_status` method may be used to retrieve the current status.
    StatusChanged,
    /// A RxBuffer was received from the Ethernet device.
    Receive(buffer::RxBuffer),
}

impl Stream for EventStream {
    type Item = Result<Event, zx::Status>;

    fn poll_next(mut self: PinMut<Self>, cx: &mut task::Context) -> Poll<Option<Self::Item>> {
        Poll::Ready(
            match ready!(self.poll_inner(cx)) {
                Ok(event) => Some(Ok(event)),
                Err(zx::Status::PEER_CLOSED) => None,
                Err(e) => Some(Err(e)),
            }
        )
    }
}

impl EventStream {
    fn poll_inner(&mut self, cx: &mut task::Context) -> Poll<Result<Event, zx::Status>> {
        loop {
            if let Poll::Ready(signals) = self.inner.poll_status(cx)? {
                if signals.contains(zx::Signals::USER_0) {
                    return Poll::Ready(Ok(Event::StatusChanged));
                }
            }

            let mut progress = false;
            if self.inner.poll_queue_tx(cx)?.is_ready() {
                progress = true;
            }

            if self.inner.poll_queue_rx(cx)?.is_ready() {
                progress = true;
            }

            // Drain the completed tx queue to make all the buffers available.
            while self.inner.poll_complete_tx(cx)?.is_ready() {
                progress = true;
            }

            if let Poll::Ready(buf) = self.inner.poll_complete_rx(cx)? {
                return Poll::Ready(Ok(Event::Receive(buf)));
            }

            if !progress {
                return Poll::Pending;
            }
        }
    }
}

#[derive(Debug)]
struct ClientInner {
    dev: File,
    pool: Mutex<buffer::BufferPool>,
    rx_fifo: fasync::Fifo<sys::eth_fifo_entry>,
    tx_fifo: fasync::Fifo<sys::eth_fifo_entry>,
    rx_depth: u32,
    tx_depth: u32,
    tx_pending: Mutex<Vec<sys::eth_fifo_entry>>,
    signals: Mutex<fasync::OnSignals>,
}

impl ClientInner {
    fn new(
        dev: File,
        pool: Mutex<buffer::BufferPool>,
        fifos: sys::eth_fifos_t,
    ) -> Result<Self, zx::Status> {
        let rx_fifo = unsafe {
            fasync::Fifo::from_fifo(zx::Fifo::from_handle(zx::Handle::from_raw(fifos.rx_fifo)))?
        };
        let tx_fifo = unsafe {
            fasync::Fifo::from_fifo(zx::Fifo::from_handle(zx::Handle::from_raw(fifos.tx_fifo)))?
        };
        let signals = Mutex::new(fasync::OnSignals::new(&rx_fifo, zx::Signals::USER_0));
        Ok(ClientInner {
            dev,
            pool,
            rx_fifo,
            tx_fifo,
            rx_depth: fifos.rx_depth,
            tx_depth: fifos.tx_depth,
            tx_pending: Mutex::new(vec![]),
            signals,
        })
    }

    fn register_signals(&self) {
        *self.signals.lock().unwrap() = fasync::OnSignals::new(&self.rx_fifo, zx::Signals::USER_0);
    }

    /// Check for Ethernet device status changes.
    ///
    /// These changes are signaled on the rx fifo.
    fn poll_status(&self, cx: &mut task::Context) -> Poll<Result<zx::Signals, zx::Status>> {
        let signals = try_ready!(self.signals.lock().unwrap().poll_unpin(cx));
        self.register_signals();
        Poll::Ready(Ok(signals))
    }

    /// Write any pending transmits to the tx fifo.
    fn poll_queue_tx(&self, cx: &mut task::Context) -> Poll<Result<usize, zx::Status>> {
        let mut tx_guard = self.tx_pending.lock().unwrap();
        if tx_guard.len() > 0 {
            let result = self.tx_fifo.try_write(&tx_guard[..], cx)?;
            // It's possible that only some of the entries were queued, so split those off the
            // pending queue and save the rest for later.
            if let Poll::Ready(n) = result {
                *tx_guard = tx_guard.split_off(n);
                return Poll::Ready(Ok(n));
            }
        }
        Poll::Pending
    }

    /// Receive a tx completion entry from the Ethernet device.
    ///
    /// Returns the flags indicating success or failure.
    fn poll_complete_tx(&self, cx: &mut task::Context) -> Poll<Result<EthernetQueueFlags, zx::Status>> {
        match try_ready!(self.tx_fifo.try_read(cx)) {
            Some(entry) => {
                self.pool
                    .lock()
                    .unwrap()
                    .release_tx_buffer(entry.offset as usize);
                Poll::Ready(Ok(EthernetQueueFlags::from_bits_truncate(
                    entry.flags,
                )))
            }
            None => Poll::Ready(Err(zx::Status::PEER_CLOSED)),
        }
    }

    /// Queue an available receive buffer to the rx fifo.
    fn poll_queue_rx(&self, cx: &mut task::Context) -> Poll<Result<(), zx::Status>> {
        try_ready!(self.rx_fifo.poll_write(cx));
        let mut pool_guard = self.pool.lock().unwrap();
        match pool_guard.alloc_rx_buffer() {
            None => Poll::Pending,
            Some(entry) => {
                let result = self.rx_fifo.try_write(&[entry], cx)?;
                if let Poll::Pending = result {
                    // Map the buffer and drop it immediately, to return it to the set of available
                    // buffers.
                    let _ = pool_guard.map_rx_buffer(entry.offset as usize, 0);
                }
                Poll::Ready(Ok(()))
            }
        }
    }

    /// Receive a buffer from the Ethernet device representing a packet from the network.
    fn poll_complete_rx(&self, cx: &mut task::Context) -> Poll<Result<buffer::RxBuffer, zx::Status>> {
        Poll::Ready(
            match try_ready!(self.rx_fifo.try_read(cx)) {
                Some(entry) => {
                    let mut pool_guard = self.pool.lock().unwrap();
                    let buf = pool_guard.map_rx_buffer(entry.offset as usize, entry.length as usize);
                    Ok(buf)
                }
                None => Err(zx::Status::PEER_CLOSED),
            }
        )
    }
}
