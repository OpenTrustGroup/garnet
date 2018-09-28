// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(async_await, await_macro)]

use std::borrow::Cow;
use std::fs;
use std::net::IpAddr;
use std::os::unix::io::AsRawFd;
use std::path;

use failure::{self, ResultExt};
use futures::{self, StreamExt, TryFutureExt, TryStreamExt};
use serde_derive::Deserialize;

use fidl_fuchsia_devicesettings::DeviceSettingsManagerMarker;
use fidl_fuchsia_netstack::{Ipv4Address, Ipv6Address, NetAddress, NetAddressFamily, NetInterface,
                            NetstackEvent, NetstackMarker};
use fidl_zircon_ethernet::{DeviceMarker, DeviceProxy, INFO_FEATURE_LOOPBACK, INFO_FEATURE_SYNTH};

mod device_id;

const DEFAULT_CONFIG_FILE: &str = "/pkg/data/default.json";
const ETHDIR: &str = "/dev/class/ethernet";

#[derive(Debug, Deserialize)]
struct Config {
    pub device_name: Option<String>,
    pub dns_config: DnsConfig,
}

#[derive(Debug, Deserialize)]
struct DnsConfig {
    pub servers: Vec<IpAddr>,
}

fn is_physical(features: u32) -> bool {
    (features & (INFO_FEATURE_SYNTH | INFO_FEATURE_LOOPBACK)) == 0
}

fn derive_device_name(interfaces: Vec<NetInterface>) -> Option<String> {
    interfaces
        .iter()
        .filter(|iface| is_physical(iface.features))
        .min_by(|a, b| a.id.cmp(&b.id))
        .map(|iface| device_id::device_id(&iface.hwaddr))
}

static DEVICE_NAME_KEY: &str = "DeviceName";

fn main() -> Result<(), failure::Error> {
    println!("netcfg: started");
    let default_config_file = fs::File::open(DEFAULT_CONFIG_FILE)
        .with_context(|_| format!("could not open config file {}", DEFAULT_CONFIG_FILE))?;
    let default_config: Config = serde_json::from_reader(default_config_file)
        .with_context(|_| format!("could not deserialize config file {}", DEFAULT_CONFIG_FILE))?;
    let mut executor = fuchsia_async::Executor::new().context("could not create executor")?;
    let netstack = fuchsia_app::client::connect_to_service::<NetstackMarker>()
        .context("could not connect to netstack")?;
    let device_settings_manager = fuchsia_app::client::connect_to_service::<
        DeviceSettingsManagerMarker,
    >().context("could not connect to device settings manager")?;

    let mut servers = default_config
        .dns_config
        .servers
        .iter()
        .map(to_net_address)
        .collect::<Vec<NetAddress>>();

    let () = netstack
        .set_name_servers(&mut servers.iter_mut())
        .context("could not set name servers")?;

    let default_device_name = default_config
        .device_name
        .as_ref()
        .map(Cow::Borrowed)
        .map(Ok);

    let mut device_name_stream = futures::stream::iter(default_device_name).chain(
        netstack.take_event_stream().try_filter_map(
            |NetstackEvent::OnInterfacesChanged { interfaces }| {
                futures::future::ok(derive_device_name(interfaces).map(Cow::Owned))
            },
        ),
    );

    let device_name = async {
        match await!(device_name_stream.try_next())
            .context("netstack event stream ended before providing interfaces")?
        {
            Some(device_name) => {
                let success =
                    await!(device_settings_manager.set_string(DEVICE_NAME_KEY, &device_name))?;
                Ok(success)
            }
            None => Err(failure::err_msg(
                "netstack event stream ended before providing interfaces",
            )),
        }
    };

    let ethdir = fs::File::open(ETHDIR).with_context(|_| format!("could not open {}", ETHDIR))?;
    let mut watcher = fuchsia_vfs_watcher::Watcher::new(&ethdir)
        .with_context(|_| format!("could not watch {}", ETHDIR))?;

    let ethernet_device = async {
        while let Some(fuchsia_vfs_watcher::WatchMessage { event, filename }) =
            await!(watcher.try_next()).with_context(|_| format!("watching {}", ETHDIR))?
        {
            match event {
                fuchsia_vfs_watcher::WatchEvent::ADD_FILE
                | fuchsia_vfs_watcher::WatchEvent::EXISTING => {
                    let filename = path::Path::new(ETHDIR).join(filename);
                    let device = fs::File::open(&filename)
                        .with_context(|_| format!("could not open {}", filename.display()))?;
                    let topological_path =
                        fdio::device_get_topo_path(&device).with_context(|_| {
                            format!("fdio::device_get_topo_path({})", filename.display())
                        })?;
                    let device = device.as_raw_fd();
                    let mut client = 0;
                    // Safe because we're passing a valid fd.
                    let () = match fuchsia_zircon::Status::from_raw(unsafe {
                        fdio::fdio_sys::fdio_get_service_handle(device, &mut client)
                    }) {
                        fuchsia_zircon::Status::OK => Ok(()),
                        status => Err(status.into_io_error()),
                    }.with_context(|_| {
                        format!(
                            "fuchsia_zircon::sys::fdio_get_service_handle({})",
                            filename.display()
                        )
                    })?;
                    // Safe because we checked the return status above.
                    let client = fuchsia_zircon::Channel::from(unsafe {
                        fuchsia_zircon::Handle::from_raw(client)
                    });

                    let device =
                        fidl::endpoints::ClientEnd::<DeviceMarker>::new(client).into_proxy()?;
                    let device_info = await!(device.get_info())?;
                    if is_physical(device_info.features) {
                        let client = device
                            .into_channel()
                            .map_err(|DeviceProxy { .. }| {
                                failure::err_msg("failed to convert device proxy into channel")
                            })?.into_zx_channel();
                        let () = netstack
                            .add_ethernet_device(
                                &topological_path,
                                fidl::endpoints::ClientEnd::<DeviceMarker>::new(client),
                            ).with_context(|_| {
                                format!(
                                    "fidl_netstack::Netstack::add_ethernet_device({})",
                                    filename.display()
                                )
                            })?;
                    }
                }
                fuchsia_vfs_watcher::WatchEvent::IDLE
                | fuchsia_vfs_watcher::WatchEvent::REMOVE_FILE => {}
                event => {
                    let () = Err(failure::format_err!("unknown WatchEvent {:?}", event))?;
                }
            }
        }
        Ok(())
    };

    let (_success, ()) = executor.run_singlethreaded(device_name.try_join(ethernet_device))?;
    Ok(())
}

fn to_net_address(addr: &IpAddr) -> NetAddress {
    match addr {
        IpAddr::V4(v4addr) => NetAddress {
            family: NetAddressFamily::Ipv4,
            ipv4: Some(Box::new(Ipv4Address {
                addr: v4addr.octets(),
            })),
            ipv6: None,
        },
        IpAddr::V6(v6addr) => NetAddress {
            family: NetAddressFamily::Ipv6,
            ipv4: None,
            ipv6: Some(Box::new(Ipv6Address {
                addr: v6addr.octets(),
            })),
        },
    }
}
