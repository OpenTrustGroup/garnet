// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

#[macro_use]
extern crate clap;
extern crate failure;
extern crate fidl;
extern crate fidl_wlan_device as wlan;
extern crate fidl_wlan_device_service as wlan_service;
extern crate fuchsia_app as component;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate futures;
#[macro_use]
extern crate structopt;

use component::client::connect_to_service;
use failure::{Error, Fail, ResultExt};
use futures::prelude::*;
use structopt::StructOpt;
use wlan_service::{DeviceServiceMarker, DeviceServiceProxy};

mod opts;
use opts::*;

type WlanSvc = DeviceServiceProxy;

fn main() -> Result<(), Error> {
    let opt = Opt::from_args();
    println!("{:?}", opt);

    let mut exec = async::Executor::new().context("error creating event loop")?;
    let wlan_svc =
        connect_to_service::<DeviceServiceMarker>().context("failed to connect to device service")?;

    let fut = match opt {
        Opt::Phy(cmd) => do_phy(cmd, wlan_svc).left_future(),
        Opt::Iface(cmd) => do_iface(cmd, wlan_svc).right_future(),
    };

    exec.run_singlethreaded(fut)
}

fn do_phy(cmd: opts::PhyCmd, wlan_svc: WlanSvc) -> impl Future<Item = (), Error = Error> {
    match cmd {
        opts::PhyCmd::List => {
            // TODO(tkilbourn): add timeouts to prevent hanging commands
            wlan_svc
                .list_phys()
                .map_err(|e| e.context("error getting response").into())
                .and_then(|response| {
                    println!("response: {:?}", response);
                    Ok(())
                }).left_future()
        },
        opts::PhyCmd::Query { phy_id } => {
            let mut req = wlan_service::QueryPhyRequest { phy_id };
            wlan_svc
                .query_phy(&mut req)
                .map_err(|e| e.context("error querying phy").into())
                .and_then(|response| {
                    println!("response: {:?}", response);
                    Ok(())
                }).right_future()
        }
    }
}

fn do_iface(cmd: opts::IfaceCmd, wlan_svc: WlanSvc) -> impl Future<Item = (), Error = Error> {
    match cmd {
        opts::IfaceCmd::New { phy_id, role } => {
            let mut req = wlan_service::CreateIfaceRequest {
                phy_id: phy_id,
                role: role.into(),
            };

            wlan_svc
                .create_iface(&mut req)
                .map_err(|e| e.context("error getting response").into())
                .and_then(|response| {
                    println!("response: {:?}", response);
                    Ok(())
                })
                .left_future()
        }
        opts::IfaceCmd::Delete { phy_id, iface_id } => {
            let mut req = wlan_service::DestroyIfaceRequest {
                phy_id: phy_id,
                iface_id: iface_id,
            };

            wlan_svc
                .destroy_iface(&mut req)
                .map(move |status| match zx::Status::ok(status) {
                    Ok(()) => println!("destroyed iface {:?}", iface_id),
                    Err(s) => println!("error destroying iface: {:?}", s),
                })
                .map_err(|e| e.context("error destroying iface").into())
                .into_future()
                .right_future()
        }
    }
}
