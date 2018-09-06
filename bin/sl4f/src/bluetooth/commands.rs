// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async;
use async::{temp::TempFutureExt, TimeoutExt};
use bt::error::Error as BTError;
use failure::Error;
use fidl_ble::{AdvertisingData, ScanFilter};
use fidl_gatt::ServiceInfo;
use futures::future::ready as fready;
use futures::prelude::*;
use futures::FutureExt;
use parking_lot::RwLock;
use serde_json::{to_value, Value};
use std::sync::Arc;
use zx::prelude::*;

// Bluetooth-related functionality
use bluetooth::constants::*;
use bluetooth::facade::BluetoothFacade;
use bluetooth::types::{BleConnectPeripheralResponse, BluetoothMethod};

macro_rules! parse_arg {
    ($args:ident, $func:ident, $name:expr) => {
        match $args.get($name) {
            Some(v) => match v.$func() {
                Some(val) => Ok(val),
                None => Err(BTError::new(format!("malformed {}", $name).as_str())),
            },
            None => Err(BTError::new(format!("{} missing", $name).as_str())),
        }
    };
}

// Takes a serde_json::Value and converts it to arguments required for
// a FIDL ble_advertise command
fn ble_advertise_to_fidl(args_raw: Value) -> Result<(Option<AdvertisingData>, Option<u32>), Error> {
    let adv_data_raw = match args_raw.get("advertising_data") {
        Some(adr) => Some(adr).unwrap().clone(),
        None => return Err(BTError::new("Advertising data missing.").into()),
    };

    let interval_raw = match args_raw.get("interval_ms") {
        Some(ir) => Some(ir).unwrap().clone(),
        None => return Err(BTError::new("Interval_ms missing.").into()),
    };

    // Unpack the name for advertising data, as well as interval of advertising
    let name: Option<String> = adv_data_raw["name"].as_str().map(String::from);
    let interval: Option<u32> = interval_raw.as_u64().map(|i| i as u32);

    // TODO(NET-1026): Is there a better way to unpack the args into an AdvData
    // struct? Unfortunately, can't derive deserialize for AdvData
    let ad = Some(AdvertisingData {
        name: name,
        tx_power_level: None,
        appearance: None,
        service_uuids: None,
        service_data: None,
        manufacturer_specific_data: None,
        solicited_service_uuids: None,
        uris: None,
    });

    fx_log_info!(tag: "ble_advertise_to_fidl", "AdvData: {:?}", ad);

    Ok((ad, interval))
}

// Takes a serde_json::Value and converts it to arguments required for a FIDL
// ble_scan command
fn ble_scan_to_fidl(
    args_raw: Value,
) -> Result<(Option<ScanFilter>, Option<u64>, Option<u64>), Error> {
    let timeout_raw = match args_raw.get("scan_time_ms") {
        Some(t) => Some(t).unwrap().clone(),
        None => return Err(BTError::new("Timeout_ms missing.").into()),
    };

    let scan_filter_raw = match args_raw.get("filter") {
        Some(f) => Some(f).unwrap().clone(),
        None => return Err(BTError::new("Scan filter missing.").into()),
    };

    let scan_count_raw = match args_raw.get("scan_count") {
        Some(c) => Some(c).unwrap().clone(),
        None => return Err(BTError::new("Scan count missing.").into()),
    };

    let timeout: Option<u64> = timeout_raw.as_u64();
    let name_substring: Option<String> =
        scan_filter_raw["name_substring"].as_str().map(String::from);
    let count: Option<u64> = scan_count_raw.as_u64();

    // For now, no scan profile, so default to empty ScanFilter
    let filter = Some(ScanFilter {
        service_uuids: None,
        service_data_uuids: None,
        manufacturer_identifier: None,
        connectable: None,
        name_substring: name_substring,
        max_path_loss: None,
    });

    Ok((filter, timeout, count))
}

// Takes a serde_json::Value and converts it to arguments required for a FIDL
// stop_advertising command. For stop advertise, no arguments are sent, rather
// uses current advertisement id (if it exists)
fn ble_stop_advertise_to_fidl(
    _args_raw: Value, bt_facade: Arc<RwLock<BluetoothFacade>>,
) -> Result<String, Error> {
    let adv_id = bt_facade.read().get_adv_id().clone();

    match adv_id.name {
        Some(aid) => Ok(aid.to_string()),
        None => Err(BTError::new("No advertisement id outstanding.").into()),
    }
}

fn parse_identifier(args_raw: Value) -> Result<String, Error> {
    let id_raw = match args_raw.get("identifier") {
        Some(id) => id,
        None => return Err(BTError::new("Connect peripheral identifier missing").into()),
    };

    let id = id_raw.as_str().map(String::from);

    match id {
        Some(id) => Ok(id),
        None => return Err(BTError::new("Identifier missing").into()),
    }
}

fn ble_publish_service_to_fidl(args_raw: Value) -> Result<(ServiceInfo, String), Error> {
    let id = parse_arg!(args_raw, as_u64, "id")?;
    let primary = parse_arg!(args_raw, as_bool, "primary")?;
    let type_ = parse_arg!(args_raw, as_str, "type")?;
    let local_service_id = parse_arg!(args_raw, as_str, "local_service_id")?;

    // TODO(NET-1293): Add support for GATT characterstics and includes
    let characteristics = None;
    let includes = None;

    let service_info = ServiceInfo {
        id,
        primary,
        type_: type_.to_string(),
        characteristics,
        includes,
    };
    Ok((service_info, local_service_id.to_string()))
}

// Takes ACTS method command and executes corresponding FIDL method
// Packages result into serde::Value
// To add new methods, add to the unsafe_many_futures! macro
pub fn ble_method_to_fidl(
    method_name: String, args: Value, bt_facade: Arc<RwLock<BluetoothFacade>>,
) -> impl Future<Output = Result<Value, Error>> {
    unsafe_many_futures!(
        Output,
        [
            BleAdvertise,
            BleConnectPeripheral,
            BleDisconnectPeripheral,
            BleListServices,
            BlePublishService,
            BleScan,
            BleStopAdvertise,
            Error
        ]
    );
    match BluetoothMethod::from_str(method_name) {
        BluetoothMethod::BleAdvertise => {
            let (ad, interval) = match ble_advertise_to_fidl(args) {
                Ok((adv_data, intv)) => (adv_data, intv),
                Err(e) => return Output::Error(fready(Err(e))),
            };

            let adv_fut = start_adv_async(bt_facade.clone(), ad, interval);
            Output::BleAdvertise(adv_fut)
        }
        BluetoothMethod::BleScan => {
            let (filter, timeout, count) = match ble_scan_to_fidl(args) {
                Ok((f, t, c)) => (f, t, c),
                Err(e) => return Output::Error(fready(Err(e))),
            };

            let scan_fut = start_scan_async(bt_facade.clone(), filter, timeout, count);
            Output::BleScan(scan_fut)
        }
        BluetoothMethod::BleStopAdvertise => {
            let advertisement_id = match ble_stop_advertise_to_fidl(args, bt_facade.clone()) {
                Ok(aid) => aid,
                Err(e) => return Output::Error(fready(Err(e))),
            };

            let stop_fut = stop_adv_async(bt_facade.clone(), advertisement_id.clone());
            Output::BleStopAdvertise(stop_fut)
        }
        BluetoothMethod::BleConnectPeripheral => {
            let id = match parse_identifier(args) {
                Ok(id) => id,
                Err(e) => return Output::Error(fready(Err(e))),
            };

            let connect_periph_fut = connect_peripheral_async(bt_facade.clone(), id.clone());
            Output::BleConnectPeripheral(connect_periph_fut)
        }
        BluetoothMethod::BleDisconnectPeripheral => {
            let id = match parse_identifier(args) {
                Ok(id) => id,
                Err(e) => return Output::Error(fready(Err(e))),
            };

            let disc_periph_fut = disconnect_peripheral_async(bt_facade.clone(), id.clone());
            Output::BleDisconnectPeripheral(disc_periph_fut)
        }
        BluetoothMethod::BleListServices => {
            let id = match parse_identifier(args) {
                Ok(id) => id,
                Err(e) => return Output::Error(fready(Err(e))),
            };

            let list_services_fut = list_services_async(bt_facade.clone(), id.clone());
            Output::BleListServices(list_services_fut)
        }
        BluetoothMethod::BlePublishService => {
            let (service_info, local_service_id) = match ble_publish_service_to_fidl(args) {
                Ok((si, pk)) => (si, pk),
                Err(e) => return Output::Error(fready(Err(e))),
            };

            let publish_service_fut =
                publish_service_async(bt_facade.clone(), service_info, local_service_id);
            Output::BlePublishService(publish_service_fut)
        }
        _ => Output::Error(fready(Err(BTError::new("Invalid BLE FIDL method").into()))),
    }
}

fn start_adv_async(
    bt_facade: Arc<RwLock<BluetoothFacade>>, ad: Option<AdvertisingData>, interval: Option<u32>,
) -> impl Future<Output = Result<Value, Error>> {
    let start_adv_fut = BluetoothFacade::start_adv(bt_facade.clone(), ad, interval);

    start_adv_fut.then(move |res| match res {
        Ok(_) => {
            let aid_response = bt_facade.read().get_adv_id();
            match to_value(aid_response) {
                Ok(val) => fready(Ok(val)),
                Err(e) => fready(Err(e.into())),
            }
        }
        Err(e) => fready(Err(e.into())),
    })
}

fn stop_adv_async(
    bt_facade: Arc<RwLock<BluetoothFacade>>, advertisement_id: String,
) -> impl Future<Output = Result<Value, Error>> {
    let stop_adv_fut = bt_facade.write().stop_adv(advertisement_id);

    stop_adv_fut.then(move |res| {
        BluetoothFacade::cleanup_peripheral(bt_facade.clone());
        match res {
            Ok(r) => match to_value(r) {
                Ok(val) => fready(Ok(val)),
                Err(e) => fready(Err(e.into())),
            },
            Err(e) => fready(Err(e.into())),
        }
    })
}

// Synchronous wrapper for scanning
fn start_scan_async(
    bt_facade: Arc<RwLock<BluetoothFacade>>, filter: Option<ScanFilter>, timeout: Option<u64>,
    count: Option<u64>,
) -> impl Future<Output = Result<Value, Error>> {
    let timeout_ms = (timeout.unwrap_or(DEFAULT_SCAN_TIMEOUT_MS) as i64).millis();
    // Create scanning future and listen on central events for scan
    let scan_fut = BluetoothFacade::start_scan(bt_facade.clone(), filter);

    // Based on if a scan count is provided, either use TIMEOUT as termination criteria
    // or custom future to terminate when <count> remote devices are discovered
    let event_fut = if count.is_none() {
        async::Timer::new(timeout_ms.after_now()).left_future()
    } else {
        // Future resolves when number of devices discovered is equal to count
        let custom_fut =
            BluetoothFacade::new_devices_found_future(bt_facade.clone(), count.unwrap());

        // Chain the custom future with a timeout
        custom_fut
            .on_timeout(timeout_ms.after_now(), || ())
            .right_future()
    };

    let fut = scan_fut.and_then(move |_| event_fut.map(Ok));

    // Grab the central proxy created
    let facade = bt_facade.clone();
    let central = facade
        .read()
        .get_central_proxy()
        .clone()
        .expect("No central proxy.");

    // After futures resolve, grab set of devices discovered and stop the scan
    fut.then(move |_| {
        let devices = bt_facade.read().get_devices();

        if let Err(e) = central.stop_scan() {
            fready(Err(e.into()))
        } else {
            match to_value(devices) {
                Ok(dev) => fready(Ok(dev)),
                Err(e) => fready(Err(e.into())),
            }
        }
    })
}

fn connect_peripheral_async(
    bt_facade: Arc<RwLock<BluetoothFacade>>, id: String,
) -> impl Future<Output = Result<Value, Error>> {
    let connect_periph_fut = BluetoothFacade::connect_peripheral(bt_facade.clone(), id.clone());

    let list_services_fut = connect_periph_fut
        .and_then(move |_| BluetoothFacade::list_services(bt_facade.clone(), id.clone()));

    list_services_fut.then(move |res| match res {
        Ok(r) => {
            let result = BleConnectPeripheralResponse::new(r);
            match to_value(result) {
                Ok(val) => fready(Ok(val)),
                Err(e) => fready(Err(e.into())),
            }
        }
        Err(e) => fready(Err(e.into())),
    })
}

fn disconnect_peripheral_async(
    bt_facade: Arc<RwLock<BluetoothFacade>>, id: String,
) -> impl Future<Output = Result<Value, Error>> {
    let disconnect_periph_fut =
        BluetoothFacade::disconnect_peripheral(bt_facade.clone(), id.clone());

    disconnect_periph_fut.then(move |res| match res {
        Ok(r) => match to_value(r) {
            Ok(val) => fready(Ok(val)),
            Err(e) => fready(Err(e.into())),
        },
        Err(e) => fready(Err(e.into())),
    })
}

// Uses the same return type as connect_peripheral_async -- Returns subset of
// fidl::ServiceInfo
fn list_services_async(
    bt_facade: Arc<RwLock<BluetoothFacade>>, id: String,
) -> impl Future<Output = Result<Value, Error>> {
    let list_services_fut = BluetoothFacade::list_services(bt_facade.clone(), id.clone());

    list_services_fut.then(move |res| match res {
        Ok(r) => {
            let result = BleConnectPeripheralResponse::new(r);
            match to_value(result) {
                Ok(val) => fready(Ok(val)),
                Err(e) => fready(Err(e.into())),
            }
        }
        Err(e) => fready(Err(e.into())),
    })
}

fn publish_service_async(
    bt_facade: Arc<RwLock<BluetoothFacade>>, service_info: ServiceInfo, local_service_id: String,
) -> impl Future<Output = Result<Value, Error>> {
    let publish_fut =
        BluetoothFacade::publish_service(bt_facade.clone(), service_info, local_service_id);

    publish_fut.then(move |res| match res {
        Ok(r) => match to_value(r) {
            Ok(val) => fready(Ok(val)),
            Err(e) => fready(Err(e.into())),
        },
        Err(e) => fready(Err(e.into())),
    })
}
