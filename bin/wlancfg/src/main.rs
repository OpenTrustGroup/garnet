// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api, pin, arbitrary_self_types, transpose_result)]
#![deny(warnings)]

mod config;
mod client;
mod device;
mod known_ess_store;
mod shim;
mod state_machine;

use crate::{config::Config, known_ess_store::KnownEssStore};

use {
    failure::{format_err, Error, ResultExt},
    fidl::endpoints::{RequestStream, ServiceMarker},
    fidl_fuchsia_wlan_device_service::DeviceServiceMarker,
    fidl_fuchsia_wlan_service as legacy,
    fuchsia_app::server::ServicesServer,
    fuchsia_async as fasync,
    futures::prelude::*,
    std::sync::Arc,
};

#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum Never {}
impl Never {
    pub fn into_any<T>(self) -> T { match self {} }
}

fn serve_fidl(_client_ref: shim::ClientRef)
    -> impl Future<Output = Result<Never, Error>>
{
    future::ready(ServicesServer::new()
        .add_service((legacy::WlanMarker::NAME, move |channel| {
            let stream = legacy::WlanRequestStream::from_channel(channel);
            let fut = shim::serve_legacy(stream, _client_ref.clone())
                .unwrap_or_else(|e| eprintln!("error serving legacy wlan API: {}", e));
            fasync::spawn(fut)
        }))
        .start())
        .and_then(|fut| fut)
        .and_then(|()| future::ready(Err(format_err!("FIDL server future exited unexpectedly"))))
}

fn main() -> Result<(), Error> {
    let cfg = Config::load_from_file()?;

    let mut executor = fasync::Executor::new().context("error creating event loop")?;
    let wlan_svc = fuchsia_app::client::connect_to_service::<DeviceServiceMarker>()
        .context("failed to connect to device service")?;

    let legacy_client = shim::ClientRef::new();
    let fidl_fut = serve_fidl(legacy_client.clone());

    let (watcher_proxy, watcher_server_end) = fidl::endpoints::create_proxy()?;
    wlan_svc.watch_devices(watcher_server_end)?;
    let listener = device::Listener::new(wlan_svc, cfg, legacy_client);
    let ess_store = Arc::new(KnownEssStore::new()?);
    let fut = watcher_proxy.take_event_stream()
        .try_for_each(|evt| device::handle_event(&listener, evt, Arc::clone(&ess_store)).map(Ok))
        .err_into()
        .and_then(|_| future::ready(Err(format_err!("Device watcher future exited unexpectedly"))));

    executor
        .run_singlethreaded(fidl_fut.try_join(fut))
        .map(|_: (Never, Never)| ())
}
