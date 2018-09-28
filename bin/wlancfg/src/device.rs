// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    config::Config,
    client,
    known_ess_store::KnownEssStore,
    shim,
};

use {
    failure::{bail, format_err},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_wlan_device as wlan,
    fidl_fuchsia_wlan_device_service::{
        self as wlan_service,
        DeviceWatcherEvent,
        DeviceServiceProxy
    },
    fuchsia_async as fasync,
    fuchsia_zircon as zx,
    futures::{
        prelude::*,
        stream,
    },
    std::sync::Arc,
};

pub struct Listener {
    proxy: DeviceServiceProxy,
    config: Config,
    legacy_client: shim::ClientRef,
}

pub async fn handle_event(listener: &Listener, evt: DeviceWatcherEvent,
                          ess_store: Arc<KnownEssStore>)
{
    println!("wlancfg got event: {:?}", evt);
    match evt {
        DeviceWatcherEvent::OnPhyAdded { phy_id } => {
            if let Err(e) = await!(on_phy_added(listener, phy_id)) {
                println!("wlancfg: error adding new phy {}: {}", phy_id, e);
            }
        },
        DeviceWatcherEvent::OnPhyRemoved { phy_id } => {
            println!("wlancfg: phy removed: {}", phy_id);
        },
        DeviceWatcherEvent::OnIfaceAdded { iface_id } => {
            if let Err(e) = await!(on_iface_added(listener, iface_id, ess_store)) {
                println!("wlancfg: error adding new iface {}: {}", iface_id, e);
            }
        },
        DeviceWatcherEvent::OnIfaceRemoved { iface_id } => {
            listener.legacy_client.remove_if_matching(iface_id);
            println!("wlancfg: iface removed: {}", iface_id);
        },
    }
}

async fn on_phy_added(listener: &Listener, phy_id: u16) -> Result<(), failure::Error> {
    println!("wlancfg: phy {} added", phy_id);
    let info = await!(query_phy(listener, phy_id))?;
    let path = info.dev_path.unwrap_or("*".into());
    println!("wlancfg: received a PhyInfo from phy #{}: path is {}", phy_id, path);
    let roles_to_create = listener.config.roles_for_path(&path).ok_or_else(
            || format_err!("no matching roles"))?;

    let mut futures = stream::FuturesUnordered::new();
    for role in roles_to_create {
        println!("wlancfg: Creating {:?} iface for phy {}", role, phy_id);
        let mut req = wlan_service::CreateIfaceRequest {
            phy_id,
            role: wlan::MacRole::from(*role),
        };
        let fut = listener.proxy.create_iface(&mut req).map(move |res| (res, role));
        futures.push(fut);
    }

    while let Some((res, role)) = await!(futures.next()) {
        if let Err(e) = res {
            println!("wlancfg: error creating iface for role {:?} and phy {}: {:?}",
                     role, phy_id, e);
        }
    }
    Ok(())
}

async fn query_phy(listener: &Listener, phy_id: u16) -> Result<wlan::PhyInfo, failure::Error> {
    let req = &mut wlan_service::QueryPhyRequest { phy_id };
    let (status, query_resp) = await!(listener.proxy.query_phy(req))
        .map_err(|e| format_err!("failed to send a query request: {:?}", e))?;
    if let Err(e) = zx::Status::ok(status) {
        bail!("query_phy returned an error: {:?}", e);
    }
    let info = query_resp.ok_or_else(|| format_err!(
            "query_phy failed to return a PhyInfo in the QueryPhyResponse"))?.info;
    Ok(info)
}

async fn on_iface_added(listener: &Listener, iface_id: u16, ess_store: Arc<KnownEssStore>)
    -> Result<(), failure::Error>
{
    let service = listener.proxy.clone();
    let legacy_client = listener.legacy_client.clone();
    let (sme, remote) = create_proxy()
        .map_err(|e| format_err!("Failed to create a FIDL channel: {}", e))?;

    let status = await!(service.get_client_sme(iface_id, remote))
        .map_err(|e| format_err!("Failed to get client SME: {}", e))?;

    zx::Status::ok(status)
        .map_err(|e| format_err!("GetClientSme returned an error: {}", e))?;

    let (c, fut) = client::new_client(iface_id, sme.clone(), ess_store);
    fasync::spawn(fut);
    let lc = shim::Client { service, client: c, sme, iface_id };
    legacy_client.set_if_empty(lc);
    println!("wlancfg: new iface {} added successfully", iface_id);
    Ok(())
}

impl Listener {
    pub fn new(proxy: DeviceServiceProxy, config: Config, legacy_client: shim::ClientRef) -> Self {
        Listener { proxy, config, legacy_client }
    }
}
