// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async;
use fidl::encoding2::OutOfLine;
use fidl_bluetooth;
use fidl_bluetooth_control::{Control, ControlImpl};
use futures::{future, Future, FutureExt, Never};
use futures::future::Either::{Left, Right};
use futures::prelude::*;
use host_dispatcher::*;
use parking_lot::RwLock;
use std::sync::Arc;
use zx::Duration;
use async::TimeoutExt;

pub static TIMEOUT: u64 = 3; // Seconds

struct ControlServiceState {
    host: Arc<RwLock<HostDispatcher>>,
    discovery_token: Option<Arc<DiscoveryRequestToken>>,
    discoverable_token: Option<Arc<DiscoverableRequestToken>>,
}

/// Build the ControlImpl to interact with fidl messages
/// State is stored in the HostDispatcher object
pub fn make_control_service(
    hd: Arc<RwLock<HostDispatcher>>, chan: async::Channel
) -> impl Future<Item = (), Error = Never> {
    ControlImpl {
        state: Arc::new(RwLock::new(ControlServiceState {
            host: hd,
            discovery_token: None,
            discoverable_token: None,
        })),
        on_open: |state, handle| {
            let wstate = state.write();
            let mut hd = wstate.host.write();
            hd.events = Some(handle.clone());
            future::ok(())
        },
        connect: |_, _, _, res| {
            res.send(&mut bt_fidl_status!(NotSupported))
                .into_future()
                .recover(|e| eprintln!("error sending response: {:?}", e))
        },
        disconnect: |_, _, res| {
            res.send(&mut bt_fidl_status!(NotSupported))
                .into_future()
                .recover(|e| eprintln!("error sending response: {:?}", e))
        },
        forget: |_, _, res| {
            res.send(&mut bt_fidl_status!(NotSupported))
                .into_future()
                .recover(|e| eprintln!("error sending response: {:?}", e))
        },
        set_name: |state, name, res| {
            let wstate = state.write();
            let mut hd = wstate.host.write();
            hd.set_name(name)
                .and_then(move |mut resp| res.send(&mut resp))
                .recover(|e| eprintln!("error sending response: {:?}", e))
        },
        get_active_adapter_info: |state, res| {
            let wstate = state.write();
            let mut hd = wstate.host.write();
            let mut adap = hd.get_active_adapter_info();

            res.send(adap.as_mut().map(OutOfLine))
                .into_future()
                .recover(|e| eprintln!("error sending response: {:?}", e))
        },
        get_known_remote_devices: |_state, _res| future::ok(()),
        get_adapters: |state, res| {
            let wstate = state.write();
            let mut hd = wstate.host.clone();
            HostDispatcher::get_adapters(&mut hd)
                .on_timeout(Duration::from_seconds(TIMEOUT).after_now(), || {
                    eprintln!("Timed out waiting for adapters");
                    Ok(vec![])
                }).unwrap()
                .and_then(move |mut resp| res.send(Some(&mut resp.iter_mut())))
                .recover(|e| eprintln!("error sending response: {:?}", e))
        },
        is_bluetooth_available: |state, res| {
            let rstate = state.read();
            let mut hd = rstate.host.write();
            let is_available = hd.get_active_adapter_info().is_some();
            res.send(is_available)
                .into_future()
                .recover(|e| eprintln!("error sending response: {:?}", e))
        },
        request_discovery: |state, discover, res| {
            let fut = if discover {
                let stateref = Arc::clone(&state);
                Left(
                    HostDispatcher::start_discovery(&state.read().host).and_then(
                        move |(mut resp, token)| {
                            stateref.write().discovery_token = token;
                            res.send(&mut resp).into_future()
                        },
                    ),
                )
            } else {
                state.write().discovery_token = None;
                Right(res.send(&mut bt_fidl_status!()).into_future())
            };
            fut.recover(|e| eprintln!("error sending response: {:?}", e))
        },
        set_discoverable: |state, discoverable, res| {
            let fut = if discoverable {
                let stateref = Arc::clone(&state);
                Left(
                    HostDispatcher::set_discoverable(&state.read().host).and_then(
                        move |(mut resp, token)| {
                            stateref.write().discoverable_token = token;
                            res.send(&mut resp).into_future()
                        },
                    ),
                )
            } else {
                state.write().discoverable_token = None;
                Right(res.send(&mut bt_fidl_status!()).into_future())
            };
            fut.recover(|e| eprintln!("error sending response: {:?}", e))
        },
        set_active_adapter: |state, adapter, res| {
            let wstate = state.write();
            let mut success = wstate.host.write().set_active_adapter(adapter.clone());
            res.send(&mut success)
                .into_future()
                .recover(|e| eprintln!("error sending response: {:?}", e))
        },
        set_pairing_delegate: |state, _, _, delegate, _res| {
            if let Some(delegate) = delegate {
                if let Ok(proxy) = delegate.into_proxy() {
                    let mut wstate = state.write();
                    wstate.host.write().pairing_delegate = Some(proxy);
                }
            }
            future::ok(())
        },
    }.serve(chan)
        .recover(|e| eprintln!("error running service: {:?}", e))
}
