// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bt::error::Error as BTError;
use common::constants::*;
use failure::{Error, Fail};
use fidl_ble::CentralProxy;
use fidl_ble::{AdvertisingData, PeripheralProxy};
use futures::future;
use futures::future::Either::{Left, Right};
use futures::prelude::*;

// BluetoothFacade: Stores Central and Peripheral proxies used for
// bluetooth scan and advertising requests.
//
// This object is shared among all threads created by server.
//
// Future plans: Object will store other common information like RemoteDevices
// found via scan, allowing for ease of state transfer between similar/related
// requests.
//
// Use: Create once per server instantiation. Calls to update_peripheral()
// and update_central() will update Facade object with proxy if no such proxy
// currently exists. If such a proxy exists, then update() will use pre-existing
// proxy.
#[derive(Debug)]
pub struct BluetoothFacade {
    // central: CentralProxy used for Bluetooth connections
    central: Option<CentralProxy>,

    //peripheral: PeripheralProxy used for Bluetooth Connections
    peripheral: Option<PeripheralProxy>,
}

impl BluetoothFacade {
    pub fn new(
        central_proxy: Option<CentralProxy>,
        peripheral_proxy: Option<PeripheralProxy>,
    ) -> BluetoothFacade {
        BluetoothFacade {
            central: central_proxy,
            peripheral: peripheral_proxy,
        }
    }

    pub fn update_peripheral(&mut self, peripheral_proxy: PeripheralProxy) {
        let new_periph = match self.peripheral.clone() {
            Some(p) => Some(p),
            None => Some(peripheral_proxy),
        };

        self.peripheral = new_periph;
    }

    pub fn start_adv(
        &self,
        adv_data: Option<AdvertisingData>,
        interval: Option<u32>,
    ) -> impl Future<Item = (), Error = Error> {
        //Default interval (ms) to 1 second
        let intv: u32 = interval.unwrap_or(DEFAULT_BLE_ADV_INTERVAL_MS);

        let mut ad = match adv_data {
            Some(ad) => ad,
            None => AdvertisingData {
                name: None,
                tx_power_level: None,
                appearance: None,
                service_uuids: None,
                service_data: None,
                manufacturer_specific_data: None,
                solicited_service_uuids: None,
                uris: None,
            },
        };

        match &self.peripheral {
            Some(p) => Right(
                p.start_advertising(&mut ad, None, intv, false)
                    .map_err(|e| e.context("failed to initiate advertise.").into())
                    .and_then(|(status, aid)| match status.error {
                        None => {
                            eprintln!("Started advertising id: {:?}", aid);
                            Ok(())
                        }
                        Some(e) => {
                            let err = BTError::from(*e);
                            eprintln!("Failed to start adveritising: {:?}", err);
                            Err(err.into())
                        }
                    }),
            ),
            None => Left(future::err(
                BTError::new("No peripheral proxy created.").into(),
            )),
        }
    }
}
