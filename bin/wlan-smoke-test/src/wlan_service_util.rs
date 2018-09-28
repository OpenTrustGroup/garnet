// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, Error, ResultExt};
use fidl::endpoints;
use fidl_fuchsia_wlan_device_service as wlan_service;
use fidl_fuchsia_wlan_sme as fidl_sme;
use fuchsia_async::{self as fasync, temp::TempStreamExt};
use fuchsia_syslog::{self as syslog, fx_log, fx_log_err};
use fuchsia_zircon as zx;
use futures::future;
use futures::prelude::*;
use pin_utils::pin_mut;
use std::fmt;
use fidl_fuchsia_wlan_device_service::DeviceServiceProxy;

type WlanService = DeviceServiceProxy;

// Helper object to formate BSSIDs
pub struct Bssid(pub [u8; 6]);

impl fmt::Display for Bssid {
    fn fmt(&self, f: &mut fmt::Formatter) -> Result<(), fmt::Error> {
        write!(f, "{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
               self.0[0], self.0[1], self.0[2], self.0[3], self.0[4], self.0[5])
    }
}

// Helper methods for calling wlan_service fidl methods

pub async fn get_iface_list(wlan_svc: &DeviceServiceProxy)
        -> Result<Vec<u16>, Error> {
    let response = await!(wlan_svc.list_ifaces()).context("Error getting iface list")?;
    let mut wlan_iface_ids = Vec::new();
    for iface in response.ifaces{
        wlan_iface_ids.push(iface.iface_id);
    }
    Ok(wlan_iface_ids)
}

pub async fn get_iface_sme_proxy(wlan_svc: &WlanService, iface_id: u16)
        -> Result<fidl_sme::ClientSmeProxy, Error> {
    let (sme_proxy, sme_remote) = endpoints::create_proxy()?;
    let status = await!(wlan_svc.get_client_sme(iface_id, sme_remote))
            .context("error sending GetClientSme request")?;
    if status == zx::sys::ZX_OK {
        Ok(sme_proxy)
    } else {
        Err(format_err!("Invalid interface id {}", iface_id))
    }
}

pub async fn connect_to_network(iface_sme_proxy: &fidl_sme::ClientSmeProxy,
                                   target_ssid: Vec<u8>,
                                   target_pwd: Vec<u8>)
        -> Result<bool, Error> {
    let (connection_proxy, connection_remote) = endpoints::create_proxy()?;

    // create ConnectRequest holding network info
    let mut req = fidl_sme::ConnectRequest {
        ssid: target_ssid,
        password: target_pwd,
        params: fidl_sme::ConnectPhyParams {
            override_phy: false,
            phy: fidl_sme::Phy::Ht,
            override_cbw: false,
            cbw: fidl_sme::Cbw::Cbw20
        }
    };

    let result = iface_sme_proxy.connect(&mut req, Some(connection_remote))?;

    let connection_code = await!(handle_connect_transaction(connection_proxy))?;

    let connected = match connection_code {
        fidl_sme::ConnectResultCode::Success => true,
        fidl_sme::ConnectResultCode::Canceled => {
            fx_log_err!("Connecting was canceled or superseded by another command");
            false
        },
        fidl_sme::ConnectResultCode::Failed => {
            fx_log_err!("Failed to connect to network");
            false
        },
        fidl_sme::ConnectResultCode::BadCredentials => {
            fx_log_err!("Failed to connect to network; bad credentials");
            false
        },
        e => {
            // also need to handle new result codes, generically return false here
            fx_log_err!("Failed to connect: {:?}", e);
            false
        }
    };

    Ok(connected)
}

async fn handle_connect_transaction(connect_transaction: fidl_sme::ConnectTransactionProxy)
        -> Result<fidl_sme::ConnectResultCode, Error> {

    let mut event_stream = connect_transaction.take_event_stream();

    let mut result_code = fidl_sme::ConnectResultCode::Failed;

    while let Some(evt) = await!(event_stream.try_next())
        .context("failed to receive connect result before the channel was closed")? {
            match evt {
                fidl_sme::ConnectTransactionEvent::OnFinished { code } => {
                    result_code = code;
                    break;
                }
            }
    }

    Ok(result_code)
}

#[cfg(test)]
mod tests {

    use super::*;
    use fidl::endpoints::RequestStream;
    use fidl_fuchsia_wlan_device_service as wlan_service;
    use fidl_fuchsia_wlan_device_service::{DeviceServiceMarker, DeviceServiceProxy};
    use fidl_fuchsia_wlan_device_service::{DeviceServiceRequest, DeviceServiceRequestStream};
    use fidl_fuchsia_wlan_device_service::{IfaceListItem, ListIfacesResponse};
    use fidl_fuchsia_wlan_sme::{ClientSmeMarker, ClientSmeProxy};
    use fidl_fuchsia_wlan_sme::{ClientSmeRequest, ClientSmeRequestStream};
    use fidl_fuchsia_wlan_sme::ConnectResultCode;
    use fuchsia_async as fasync;
    use futures::stream::StreamFuture;

    #[test]
    fn list_ifaces_returns_iface_id_vector() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (wlan_service, mut server) = create_wlan_service_util();
        let mut next_device_service_req = server.into_future();

        // create the data to use in the response
        let iface_id_list: Vec<u16> = vec![0, 1, 35, 36];
        let mut iface_list_vec = vec![];
        for id in &iface_id_list {
            iface_list_vec.push(IfaceListItem{iface_id: *id, path:
                                              "/foo/bar/".to_string()});
        }

        let mut fut = get_iface_list(&wlan_service);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        send_iface_list_response(&mut exec, &mut next_device_service_req, iface_list_vec);

        let complete = exec.run_until_stalled(&mut fut);

        let list_response = match complete {
            Poll::Ready(result) => result,
            _ => panic!("Expected an iface list response")
        };

        let response = match list_response {
            Ok(response) => response,
            Err(_) => panic!("Expected a valid list response")
        };

        // now verify the response
        assert_eq!(response, iface_id_list);
    }

    #[test]
    fn list_ifaces_properly_handles_zero_ifaces() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (wlan_service, mut server) = create_wlan_service_util();
        let mut next_device_service_req = server.into_future();

        // create the data to use in the response
        let iface_id_list: Vec<u16> = vec![];
        let mut iface_list_vec = vec![];

        let mut fut = get_iface_list(&wlan_service);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        send_iface_list_response(&mut exec, &mut next_device_service_req, iface_list_vec);

        let complete = exec.run_until_stalled(&mut fut);

        let list_response = match complete {
            Poll::Ready(result) => result,
            _ => panic!("Expected an iface list response")
        };

        let response = match list_response {
            Ok(response) => response,
            Err(_) => panic!("Expected a valid list response")
        };

        // now verify the response
        assert_eq!(response, iface_id_list);
    }

    fn poll_device_service_req(exec: &mut fasync::Executor,
            next_device_service_req: &mut StreamFuture<DeviceServiceRequestStream>)
        -> Poll<DeviceServiceRequest>
    {
        exec.run_until_stalled(next_device_service_req).map(|(req, stream)| {
            *next_device_service_req = stream.into_future();
            req.expect("did not expect the DeviceServiceRequestStream to end")
                .expect("error polling device service request stream")
        })
    }

    fn send_iface_list_response(exec: &mut fasync::Executor,
            server: &mut StreamFuture<wlan_service::DeviceServiceRequestStream>,
            iface_list_vec: Vec<IfaceListItem>)
    {
        let responder = match poll_device_service_req(exec, server) {
            Poll::Ready(DeviceServiceRequest::ListIfaces {responder}) => responder,
            Poll::Pending => panic!("expected a request to be available"),
            _ => panic!("expected a ListIfaces request"),
        };

        // now send the response back
        responder.send(&mut ListIfacesResponse{ifaces:iface_list_vec});
    }

    #[test]
    fn connect_to_network_success_returns_true() {
        let connect_result = test_connect("TestAp", "", ConnectResultCode::Success);
        assert!(connect_result);
    }

    #[test]
    fn connect_to_network_failed_returns_false() {
        let connect_result = test_connect("TestAp", "", ConnectResultCode::Failed);
        assert!(!connect_result);
    }

    #[test]
    fn connect_to_network_canceled_returns_false() {
        let connect_result = test_connect("TestAp", "", ConnectResultCode::Canceled);
        assert!(!connect_result);
    }

    #[test]
    fn connect_to_network_bad_credentials_returns_false() {
        let connect_result = test_connect(
                "TestAp", "", ConnectResultCode::BadCredentials);
        assert!(!connect_result);
    }

    fn test_connect(ssid: &str,
                    password: &str,
                    result_code: ConnectResultCode) -> bool {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (client_sme, mut server) = create_client_sme_proxy();
        let mut next_client_sme_req = server.into_future();

        let target_ssid = ssid.as_bytes();
        let target_password = password.as_bytes();

        let mut fut = connect_to_network(&client_sme,
                                            target_ssid.to_vec(),
                                            target_password.to_vec());
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // have the request, need to send a response
        send_connect_request_response(&mut exec, &mut next_client_sme_req,
                target_ssid, target_password, result_code);

        let complete = exec.run_until_stalled(&mut fut);
        let connection_result = match complete {
            Poll::Ready(result) => result,
            _ => panic!("Expected a connect response")
        };

        let returned_bool = match connection_result {
            Ok(response) => response,
            _ => panic!("Expected a valid connection result")
        };

        returned_bool
    }

    #[test]
    fn connect_to_network_properly_passes_network_info_with_password() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (client_sme, mut server) = create_client_sme_proxy();
        let mut next_client_sme_req = server.into_future();

        let target_ssid = "TestAp".as_bytes();
        let target_password = "password".as_bytes();

        let mut fut = connect_to_network(&client_sme,
                                            target_ssid.to_vec(),
                                            target_password.to_vec());
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // verify the connect request info
        verify_connect_request_info(&mut exec, &mut next_client_sme_req,
                target_ssid, target_password);
    }

    #[test]
    fn connect_to_network_properly_passes_network_info_open() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (client_sme, mut server) = create_client_sme_proxy();
        let mut next_client_sme_req = server.into_future();

        let target_ssid = "TestAp".as_bytes();
        let target_password = "".as_bytes();

        let mut fut = connect_to_network(&client_sme,
                                            target_ssid.to_vec(),
                                            target_password.to_vec());
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // verify the connect request info
        verify_connect_request_info(&mut exec, &mut next_client_sme_req,
                target_ssid, target_password);
    }

    fn verify_connect_request_info(exec: &mut fasync::Executor,
            server: &mut StreamFuture<ClientSmeRequestStream>,
            expected_ssid: &[u8],
            expected_password: &[u8]) {
        match poll_client_sme_request(exec, server) {
            Poll::Ready(ClientSmeRequest::Connect {req, .. }) => {
                assert_eq!(expected_ssid, &req.ssid[..]);
                assert_eq!(expected_password, &req.password[..]);
            },
            _ => panic!("expected a Connect request"),
        }
    }

    fn send_connect_request_response(exec: &mut fasync::Executor,
            server: &mut StreamFuture<ClientSmeRequestStream>,
            expected_ssid: &[u8],
            expected_password: &[u8],
            connect_result: ConnectResultCode) {
        let responder = match poll_client_sme_request(exec, server) {
            Poll::Ready(ClientSmeRequest::Connect {req, txn, .. }) => {
                assert_eq!(expected_ssid, &req.ssid[..]);
                assert_eq!(expected_password, &req.password[..]);
                txn.expect("expected a Connect transaction channel")
            },
            Poll::Pending => panic!("expected a request to be available"),
            _ => panic!("expected a Connect request"),
        };
        let connect_transaction = responder.into_stream()
                .expect("failed to create a connect transaction stream").control_handle();
        connect_transaction.send_on_finished(connect_result)
                .expect("failed to send OnFinished to ConnectTransaction");
    }

    fn poll_client_sme_request(exec: &mut fasync::Executor,
            next_client_sme_req: &mut StreamFuture<ClientSmeRequestStream>)
        -> Poll<ClientSmeRequest>
    {
        exec.run_until_stalled(next_client_sme_req).map(|(req, stream)| {
            *next_client_sme_req = stream.into_future();
            req.expect("did not expect the ClientSmeRequestStream to end")
                .expect("error polling client sme request stream")
        })
    }

    fn create_client_sme_proxy() -> (fidl_sme::ClientSmeProxy, ClientSmeRequestStream) {
        let (proxy, server) = endpoints::create_proxy::<ClientSmeMarker>()
                .expect("failed to create sme client channel");
        let server = server.into_stream()
                .expect("failed to create a client sme response stream");
        (proxy, server)
    }

    fn create_wlan_service_util()
            -> (DeviceServiceProxy, DeviceServiceRequestStream) {
        let (proxy, server) = endpoints::create_proxy::<DeviceServiceMarker>()
                .expect("failed to create a wlan_service channel for tests");
        let server = server.into_stream()
                .expect("failed to create a wlan_service response stream");
        (proxy, server)
    }
}

