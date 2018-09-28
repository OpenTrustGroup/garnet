// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    known_ess_store::{KnownEssStore, KnownEss},
    state_machine::{self, IntoStateExt},
};

use {
    failure::{bail, format_err},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_wlan_sme as fidl_sme,
    fuchsia_zircon::prelude::*,
    futures::{
        channel::{oneshot, mpsc},
        prelude::*,
        select,
        stream,
    },
    pin_utils::pin_mut,
    std::sync::Arc,
};

const AUTO_CONNECT_RETRY_SECONDS: i64 = 10;
const AUTO_CONNECT_SCAN_TIMEOUT_SECONDS: u8 = 20;

#[derive(Clone)]
pub struct Client {
    req_sender: mpsc::UnboundedSender<ManualRequest>,
}

impl Client {
    pub fn connect(&self, request: ConnectRequest) -> Result<(), failure::Error> {
        handle_send_err(self.req_sender.unbounded_send(ManualRequest::Connect(request)))
    }

    pub fn disconnect(&self, responder: oneshot::Sender<()>) -> Result<(), failure::Error> {
        handle_send_err(self.req_sender.unbounded_send(ManualRequest::Disconnect(responder)))
    }
}

fn handle_send_err(r: Result<(), mpsc::TrySendError<ManualRequest>>) -> Result<(), failure::Error> {
    r.map_err(|_| format_err!("Station does not exist anymore"))
}

pub struct ConnectRequest {
    pub ssid: Vec<u8>,
    pub password: Vec<u8>,
    pub responder: oneshot::Sender<fidl_sme::ConnectResultCode>,
}

enum ManualRequest {
    Connect(ConnectRequest),
    // The sender will be notified once we are done disconnecting.
    // If the disconnect request is canceled or superseded (e.g., by a connect request),
    // then the sender will be dropped.
    Disconnect(oneshot::Sender<()>),
}

pub fn new_client(iface_id: u16,
                  sme: fidl_sme::ClientSmeProxy,
                  ess_store: Arc<KnownEssStore>)
    -> (Client, impl Future<Output = ()>)
{
    let (req_sender, req_receiver) = mpsc::unbounded();
    let sme_event_stream = sme.take_event_stream();
    let services = Services {
        sme,
        ess_store: Arc::clone(&ess_store)
    };
    let fut = serve(iface_id, services, sme_event_stream, req_receiver);
    let client = Client { req_sender };
    (client, fut)
}

type State = state_machine::State<failure::Error>;
type NextReqFut = stream::StreamFuture<mpsc::UnboundedReceiver<ManualRequest>>;

#[derive(Clone)]
struct Services {
    sme: fidl_sme::ClientSmeProxy,
    ess_store: Arc<KnownEssStore>,
}

async fn serve(iface_id: u16,
               services: Services,
               sme_event_stream: fidl_sme::ClientSmeEventStream,
               req_stream: mpsc::UnboundedReceiver<ManualRequest>)
{
    let mut state_machine = auto_connect_state(services, req_stream.into_future())
            .into_state_machine();
    let removal_watcher = sme_event_stream.map_ok(|_| ()).try_collect::<()>();
    select! {
        state_machine => match state_machine {
            Ok(never) => never.into_any(),
            Err(e) => println!("wlancfg: Client station state machine \
                                for iface #{} terminated with an error: {}", iface_id, e)
        },
        removal_watcher => if let Err(e) = removal_watcher {
            println!("wlancfg: Error reading from Client SME channel of iface #{}: {}",
                     iface_id, e);
        },
    }
    println!("wlancfg: Removed client station for iface #{}", iface_id);
}

async fn auto_connect_state(services: Services, mut next_req: NextReqFut)
    -> Result<State, failure::Error>
{
    println!("wlancfg: Starting auto-connect loop");
    let auto_connected = auto_connect(&services);
    pin_mut!(auto_connected);
    select! {
        auto_connected => {
            let _ssid = auto_connected?;
            Ok(connected_state(services.clone(), next_req).into_state())
        },
        next_req => {
            let (req, req_stream) = next_req;
            handle_manual_request(services.clone(), req, req_stream)
        },
    }
}

fn handle_manual_request(services: Services,
                         req: Option<ManualRequest>,
                         req_stream: mpsc::UnboundedReceiver<ManualRequest>)
    -> Result<State, failure::Error>
{
    match req {
        Some(ManualRequest::Connect(req)) => {
            Ok(manual_connect_state(services, req_stream.into_future(), req).into_state())
        },
        Some(ManualRequest::Disconnect(responder)) => {
            Ok(disconnected_state(responder, services, req_stream.into_future()).into_state())
        }
        None => bail!("The stream of user requests ended unexpectedly")
    }
}

async fn auto_connect(services: &Services) -> Result<Vec<u8>, failure::Error> {
    loop {
        if let Some(ssid) = await!(attempt_auto_connect(services))? {
            return Ok(ssid);
        }
        await!(fuchsia_async::Timer::new(AUTO_CONNECT_RETRY_SECONDS.seconds().after_now()));
    }
}

async fn attempt_auto_connect(services: &Services) -> Result<Option<Vec<u8>>, failure::Error> {
    // first check if we have saved networks
    if services.ess_store.known_network_count() < 1 {
        return Ok(None);
    }

    let txn = start_scan_txn(&services.sme)?;
    let results = await!(fetch_scan_results(txn))?;
    let known_networks = results.into_iter()
        .filter_map(|ess| {
            services.ess_store.lookup(&ess.best_bss.ssid)
                .map(|known_ess| (ess.best_bss.ssid, known_ess))
        });
    for (ssid, known_ess) in known_networks {
        if await!(connect_to_known_network(&services.sme, ssid.clone(), known_ess))? {
            return Ok(Some(ssid));
        }
    }
    Ok(None)
}

async fn connect_to_known_network(sme: &fidl_sme::ClientSmeProxy, ssid: Vec<u8>, ess: KnownEss)
    -> Result<bool, failure::Error>
{
    let ssid_str = String::from_utf8_lossy(&ssid).into_owned();
    println!("wlancfg: Auto-connecting to '{}'", ssid_str);
    let txn = start_connect_txn(sme, &ssid, &ess.password)?;
    match await!(wait_until_connected(txn))? {
        fidl_sme::ConnectResultCode::Success => {
            println!("wlancfg: Auto-connected to '{}'", ssid_str);
            Ok(true)
        },
        other => {
            println!("wlancfg: Failed to auto-connect to '{}': {:?}", ssid_str, other);
            Ok(false)
        },
    }
}

async fn manual_connect_state(services: Services, mut next_req: NextReqFut, req: ConnectRequest)
    -> Result<State, failure::Error>
{
    println!("wlancfg: Connecting to '{}' because of a manual request from the user",
        String::from_utf8_lossy(&req.ssid));
    let txn = start_connect_txn(&services.sme, &req.ssid, &req.password)?;
    let connected = wait_until_connected(txn);
    pin_mut!(connected);

    select! {
        connected => {
            let code = connected?;
            req.responder.send(code).unwrap_or_else(|_| ());
            Ok(match code {
                fidl_sme::ConnectResultCode::Success => {
                    println!("wlancfg: Successfully connected to '{}'",
                             String::from_utf8_lossy(&req.ssid));
                    let ess = KnownEss { password: req.password.clone() };
                    services.ess_store.store(req.ssid.clone(), ess).unwrap_or_else(
                            |e| eprintln!("wlancfg: Failed to store network password: {}", e));
                    connected_state(services, next_req).into_state()
                },
                other => {
                    println!("wlancfg: Failed to connect to '{}': {:?}",
                             String::from_utf8_lossy(&req.ssid), other);
                    auto_connect_state(services, next_req).into_state()
                }
            })
        },
        next_req => {
            let (new_req, req_stream) = next_req;
            req.responder.send(fidl_sme::ConnectResultCode::Canceled).unwrap_or_else(|_| ());
            handle_manual_request(services, new_req, req_stream)
        },
    }
}

async fn connected_state(services: Services, next_req: NextReqFut) -> Result<State, failure::Error>
{
    // TODO(gbonik): monitor connection status and jump back to auto-connect state when disconnected
    let (req, req_stream) = await!(next_req);
    handle_manual_request(services, req, req_stream)
}

async fn disconnected_state(responder: oneshot::Sender<()>,
                            services: Services, mut next_req: NextReqFut)
    -> Result<State, failure::Error>
{
    // First, ask the SME to disconnect and wait for its response.
    // In the meantime, also listen to user requests.
    let mut responders = vec![responder];
    let mut pending_disconnect = services.sme.disconnect();
    'waiting_to_disconnect: loop {
        next_req = select! {
            pending_disconnect => {
                // If 'disconnect' call to SME failed, return an error since we can't
                // recover from it
                pending_disconnect.map_err(
                    |e| format_err!("Failed to send a disconnect command to wlanstack: {}", e))?;
                break 'waiting_to_disconnect;
            },
            next_req => {
                let (req, req_stream) = next_req;
                match req {
                    // If another disconnect request comes in, save its responder
                    Some(ManualRequest::Disconnect(responder)) => {
                        responders.push(responder);
                        req_stream.into_future()
                    },
                    // Drop all responders to indicate that disconnecting was superseded
                    // by another command and transition to an appropriate state
                    other => return handle_manual_request(services.clone(), other, req_stream),
                }
            },
        }
    }

    // Notify the user(s) that disconnect was confirmed by the SME
    for responder in responders {
        responder.send(()).unwrap_or_else(|_| ())
    }

    // Now that we are officially disconnected, wait for user requests
    loop {
        let (req, req_stream) = await!(next_req);
        next_req = match req {
            // If asked to disconnect, just reply immediately since we are already disconnected
            Some(ManualRequest::Disconnect(responder)) => {
                responder.send(()).unwrap_or_else(|_e| ());
                req_stream.into_future()
            },
            // Otherwise, handle the request normally
            other => return handle_manual_request(services.clone(), other, req_stream),
        }
    }
}

fn start_scan_txn(sme: &fidl_sme::ClientSmeProxy)
    -> Result<fidl_sme::ScanTransactionProxy, failure::Error>
{
    let (scan_txn, remote) = create_proxy()?;
    let mut req = fidl_sme::ScanRequest {
        timeout: AUTO_CONNECT_SCAN_TIMEOUT_SECONDS,
    };
    sme.scan(&mut req, remote)?;
    Ok(scan_txn)
}

fn start_connect_txn(sme: &fidl_sme::ClientSmeProxy, ssid: &[u8], password: &[u8])
    -> Result<fidl_sme::ConnectTransactionProxy, failure::Error>
{
    let (connect_txn, remote) = create_proxy()?;
    let mut req = fidl_sme::ConnectRequest {
        ssid: ssid.to_vec(),
        password: password.to_vec(),
        params: fidl_sme::ConnectPhyParams {
            override_phy: false,
            phy: fidl_sme::Phy::Ht,
            override_cbw: false,
            cbw: fidl_sme::Cbw::Cbw20
        },
    };
    sme.connect(&mut req, Some(remote))?;
    Ok(connect_txn)
}

async fn wait_until_connected(txn: fidl_sme::ConnectTransactionProxy)
    -> Result<fidl_sme::ConnectResultCode, failure::Error>
{
    let mut stream = txn.take_event_stream();
    while let Some(event) = await!(stream.try_next())? {
        match event {
            fidl_sme::ConnectTransactionEvent::OnFinished { code } => return Ok(code),
        }
    }
    Err(format_err!("Server closed the ConnectTransaction channel before sending a response"))
}

async fn fetch_scan_results(txn: fidl_sme::ScanTransactionProxy)
    -> Result<Vec<fidl_sme::EssInfo>, failure::Error>
{
    let mut stream = txn.take_event_stream();
    let mut all_aps = vec![];
    while let Some(event) = await!(stream.next()) {
        match event? {
            fidl_sme::ScanTransactionEvent::OnResult { aps } => all_aps.extend(aps),
            fidl_sme::ScanTransactionEvent::OnFinished { } => return Ok(all_aps),
            fidl_sme::ScanTransactionEvent::OnError { error } => {
                eprintln!("wlancfg: Scanning failed with error: {:?}", error);
                return Ok(all_aps)
            }
        }
    }
    eprintln!("Server closed the ScanTransaction channel before sending a Finished or Error event");
    Ok(all_aps)
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        fuchsia_async as fasync,
        fidl::endpoints::RequestStream,
        fidl_fuchsia_wlan_sme::{ClientSmeRequest, ClientSmeRequestStream},
        futures::stream::StreamFuture,
        std::path::Path,
        tempdir,
    };

    #[test]
    fn scans_only_requested_with_saved_networks() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempdir::TempDir::new("client_test").expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let (_client, fut, sme_server) = create_client(Arc::clone(&ess_store));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // the ess store should be empty
        assert_eq!(0, ess_store.known_network_count());

        // now verify that a scan was not requested
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());
        // we do not cancel the timer
        assert!(exec.wake_next_timer().is_some());

        // now add a network, and verify the count reflects it
        ess_store.store(b"bar".to_vec(), KnownEss { password: b"qwerty".to_vec() })
            .expect("failed to store a network password");
        assert_eq!(1, ess_store.known_network_count());

        // Expect the state machine to initiate the scan, then send results back
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        send_scan_results(&mut exec, &mut next_sme_req, &[&b"foo"[..], &b"bar"[..]]);
    }

    #[test]
    fn auto_connect_to_known_ess() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempdir::TempDir::new("client_test").expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        // save the network to trigger a scan
        ess_store.store(b"bar".to_vec(), KnownEss { password: b"qwerty".to_vec() })
            .expect("failed to store a network password");

        let (_client, fut, sme_server) = create_client(Arc::clone(&ess_store));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // Expect the state machine to initiate the scan, then send results back without the saved
        // network
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        send_scan_results(&mut exec, &mut next_sme_req, &[&b"foo"[..]]);

        // None of the returned ssids are known though, so expect the state machine to simply sleep
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());

        assert!(exec.wake_next_timer().is_some());
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect another scan request to the SME and send results
        send_scan_results(&mut exec, &mut next_sme_req, &[&b"foo"[..], &b"bar"[..]]);

        // Let the state machine process the results
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a "connect" request to the SME and reply to it
        exchange_connect_with_sme(&mut exec, &mut next_sme_req, b"bar", b"qwerty",
                            fidl_sme::ConnectResultCode::Success);

        // Let the state machine absorb the connect ack
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // We should be in the 'connected' state now, with no further requests to the SME
        // or pending timers
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());
        assert_eq!(None, exec.wake_next_timer());
    }

    #[test]
    fn manual_connect_cancels_auto_connect() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempdir::TempDir::new("client_test").expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let (client, fut, sme_server) = create_client(Arc::clone(&ess_store));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // Send a manual connect request and expect the state machine
        // to start connecting to the network immediately
        let mut receiver = send_manual_connect_request(&client, b"foo");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        exchange_connect_with_sme(&mut exec, &mut next_sme_req, b"foo", b"qwerty",
                            fidl_sme::ConnectResultCode::Success);

        // Let the state machine absorb the response
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a response to the user's request
        assert_eq!(Poll::Ready(Ok(fidl_sme::ConnectResultCode::Success)),
                   exec.run_until_stalled(&mut receiver));

        // Expect no other messages to SME or pending timers
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());
        assert_eq!(None, exec.wake_next_timer());

        // Network should be saved as known since we connected successfully
        let known_ess = ess_store.lookup(b"foo")
            .expect("expected 'foo' to be saved as a known network");
        assert_eq!(b"qwerty", &known_ess.password[..]);
    }

    #[test]
    fn manual_connect_cancels_manual_connect() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempdir::TempDir::new("client_test").expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let (client, fut, sme_server) = create_client(Arc::clone(&ess_store));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // Send the first manual connect request
        let mut receiver_one = send_manual_connect_request(&client, b"foo");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect the state machine to start connecting to the network immediately.
        let _connect_txn = match poll_sme_req(&mut exec, &mut next_sme_req) {
            Poll::Ready(ClientSmeRequest::Connect { req, txn, .. }) => {
                assert_eq!(b"foo", &req.ssid[..]);
                txn
            },
            _ => panic!("expected a Connect request"),
        };

        // Send another connect request without waiting for the first one to complete
        let mut receiver_two = send_manual_connect_request(&client, b"bar");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect the first request to be canceled
        assert_eq!(Poll::Ready(Ok(fidl_sme::ConnectResultCode::Canceled)),
                   exec.run_until_stalled(&mut receiver_one));

        // Expect the state machine to start connecting to the network immediately.
        // Send a successful result and let the state machine absorb it.
        exchange_connect_with_sme(&mut exec, &mut next_sme_req, b"bar", b"qwerty",
                            fidl_sme::ConnectResultCode::Success);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a response to the second request from user
        assert_eq!(Poll::Ready(Ok(fidl_sme::ConnectResultCode::Success)),
                   exec.run_until_stalled(&mut receiver_two));

        // Expect no other messages to SME or pending timers
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());
        assert_eq!(None, exec.wake_next_timer());
    }

    #[test]
    fn manual_connect_when_already_connected() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempdir::TempDir::new("client_test").expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        // save the network that will be autoconnected
        ess_store.store(b"foo".to_vec(), KnownEss { password: b"12345".to_vec() })
            .expect("failed to store a network password");

        let (client, fut, sme_server) = create_client(Arc::clone(&ess_store));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // Get the state machine into the connected state by auto-connecting to a known network
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        send_scan_results(&mut exec, &mut next_sme_req, &[&b"foo"[..]]);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        exchange_connect_with_sme(&mut exec, &mut next_sme_req, b"foo", b"12345",
                            fidl_sme::ConnectResultCode::Success);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // We should be in the connected state now, with no pending timers or messages to SME
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());
        assert_eq!(None, exec.wake_next_timer());

        // Now, send a manual connect request and expect the machine to start connecting immediately
        let mut receiver = send_manual_connect_request(&client, b"bar");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        exchange_connect_with_sme(&mut exec, &mut next_sme_req, b"bar", b"qwerty",
                            fidl_sme::ConnectResultCode::Success);

        // Let the state machine absorb the response
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a response to the user's request
        assert_eq!(Poll::Ready(Ok(fidl_sme::ConnectResultCode::Success)),
                   exec.run_until_stalled(&mut receiver));

        // Expect no other messages to SME or pending timers
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());
        assert_eq!(None, exec.wake_next_timer());
    }

    #[test]
    fn manual_connect_failure_triggers_auto_connect() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempdir::TempDir::new("client_test").expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let (client, fut, sme_server) = create_client(Arc::clone(&ess_store));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // Send a manual connect request and expect the state machine
        // to start connecting to the network immediately.
        // Reply with a failure.
        let mut receiver = send_manual_connect_request(&client, b"foo");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        exchange_connect_with_sme(&mut exec, &mut next_sme_req, b"foo", b"qwerty",
                            fidl_sme::ConnectResultCode::Failed);
        // auto connect will only scan with a saved network, make sure we have one
        ess_store.store(b"bar".to_vec(), KnownEss { password: b"qwerty".to_vec() })
            .expect("failed to store a network password");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // State machine should be in the auto-connect state now and is expected to
        // start scanning immediately
        match poll_sme_req(&mut exec, &mut next_sme_req) {
            Poll::Ready(ClientSmeRequest::Scan { .. }) => {},
            _ => panic!("expected a Scan request"),
        };

        // Expect a response to the user's request
        assert_eq!(Poll::Ready(Ok(fidl_sme::ConnectResultCode::Failed)),
                   exec.run_until_stalled(&mut receiver));

        // Expect no other messages to SME or pending timers for now
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());
        assert_eq!(None, exec.wake_next_timer());

        // Network should not be saved as known since we failed to connect
        assert_eq!(None, ess_store.lookup(b"foo"));
        assert_eq!(1, ess_store.known_network_count());
    }

    #[test]
    fn manual_connect_after_sme_disconnected() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempdir::TempDir::new("client_test").expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let (client, fut, sme_server) = create_client(Arc::clone(&ess_store));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // Transition to disconnected state
        let (sender, mut receiver) = oneshot::channel();
        client.disconnect(sender).expect("sending a disconnect request failed");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a disconnect request to SME, reply to it and absorb the response
        exchange_disconnect_with_sme(&mut exec, &mut next_sme_req);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Our disconnect request must have been processed at this point
        assert_eq!(Poll::Ready(Ok(())), exec.run_until_stalled(&mut receiver));

        // Issue a manual connect request and expect a corresponding message to SME
        let _receiver = send_manual_connect_request(&client, b"foo");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        exchange_connect_with_sme(&mut exec, &mut next_sme_req, b"foo", b"qwerty",
                            fidl_sme::ConnectResultCode::Success);
    }

    #[test]
    fn manual_connect_while_sme_is_disconnecting() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempdir::TempDir::new("client_test").expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let (client, fut, sme_server) = create_client(Arc::clone(&ess_store));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // Transition to disconnected state
        let (sender, mut receiver_one) = oneshot::channel();
        client.disconnect(sender).expect("sending a disconnect request failed (1)");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a disconnect request to SME, but don't reply to it yet
        let _disconnect_responder = expect_disconnect_req_to_sme(&mut exec, &mut next_sme_req);

        // Send another disconnect request from the user
        let (sender, mut receiver_two) = oneshot::channel();
        client.disconnect(sender).expect("sending a disconnect request failed (2)");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Don't expect another message to SME since we already sent a disconnect request
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());

        // Issue a manual connect request and expect a corresponding message to SME
        let _receiver = send_manual_connect_request(&client, b"foo");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a connect request to SME, but don't reply yet
        let _connect_txn = expect_connect_req_to_sme(&mut exec, &mut next_sme_req, b"foo", b"qwerty");

        // Both user's disconnect requests must have been marked as canceled
        assert_eq!(Poll::Ready(Err(oneshot::Canceled)), exec.run_until_stalled(&mut receiver_one));
        assert_eq!(Poll::Ready(Err(oneshot::Canceled)), exec.run_until_stalled(&mut receiver_two));
    }

    #[test]
    fn disconnect_request_when_already_disconnected() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempdir::TempDir::new("client_test").expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let (client, fut, sme_server) = create_client(Arc::clone(&ess_store));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // Transition to disconnected state
        let (sender, mut receiver) = oneshot::channel();
        client.disconnect(sender).expect("sending a disconnect request failed (1)");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a disconnect request to SME, reply to it and absorb the response
        exchange_disconnect_with_sme(&mut exec, &mut next_sme_req);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Our disconnect request must have been processed at this point
        assert_eq!(Poll::Ready(Ok(())), exec.run_until_stalled(&mut receiver));

        // Issue another disconnect request and expect a reply immediately since
        // we are already disconnected
        let (sender, mut receiver) = oneshot::channel();
        client.disconnect(sender).expect("sending a disconnect request failed (2)");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        assert_eq!(Poll::Ready(Ok(())), exec.run_until_stalled(&mut receiver));
    }

    #[test]
    fn disconnect_request_when_manually_connecting() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempdir::TempDir::new("client_test").expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let (client, fut, sme_server) = create_client(Arc::clone(&ess_store));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // Send the first manual connect request and expect a corresponding message to SME
        let mut connect_receiver = send_manual_connect_request(&client, b"foo");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        let mut connect_txn = expect_connect_req_to_sme(
                &mut exec, &mut next_sme_req, b"foo", b"qwerty");

        // Before the SME replies, issue a Disconnect request
        let (sender, _disconnect_receiver) = oneshot::channel();
        client.disconnect(sender).expect("sending a disconnect request failed");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect the connect transaction to be dropped by us
        match exec.run_until_stalled(&mut connect_txn.next()) {
            Poll::Ready(None) => {},
            _ => panic!("expected connect_txn channel to be closed by the state machine"),
        }

        // Expect a disconnect request to the SME
        let _disconnect_responder = expect_disconnect_req_to_sme(&mut exec, &mut next_sme_req);

        // User should be notified that their connect request was canceled
        assert_eq!(Poll::Ready(Ok(fidl_sme::ConnectResultCode::Canceled)),
                   exec.run_until_stalled(&mut connect_receiver));
    }

    #[test]
    fn disconnect_when_connected() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempdir::TempDir::new("client_test").expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let (client, fut, sme_server) = create_client(Arc::clone(&ess_store));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // Save the network that we will auto-connect to
        ess_store.store(b"foo".to_vec(), KnownEss { password: b"12345".to_vec() })
            .expect("failed to store a network password");

        // Get the state machine into the connected state by auto-connecting to a known network
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        send_scan_results(&mut exec, &mut next_sme_req, &[&b"foo"[..]]);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        exchange_connect_with_sme(&mut exec, &mut next_sme_req, b"foo", b"12345",
                                  fidl_sme::ConnectResultCode::Success);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Request to disconnect
        let (sender, mut receiver) = oneshot::channel();
        client.disconnect(sender).expect("sending a disconnect request failed");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a disconnect request to SME, reply to it and absorb the response
        exchange_disconnect_with_sme(&mut exec, &mut next_sme_req);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Our disconnect request must have been processed at this point
        assert_eq!(Poll::Ready(Ok(())), exec.run_until_stalled(&mut receiver));
    }

    fn send_manual_connect_request(client: &Client, ssid: &[u8])
        -> oneshot::Receiver<fidl_sme::ConnectResultCode>
    {
        let (responder, receiver) = oneshot::channel();
        client.connect(ConnectRequest {
            ssid: ssid.to_vec(),
            password: b"qwerty".to_vec(),
            responder
        }).expect("sending a connect request failed");
        receiver
    }

    fn poll_sme_req(exec: &mut fasync::Executor,
                    next_sme_req: &mut StreamFuture<ClientSmeRequestStream>)
        -> Poll<ClientSmeRequest>
    {
        exec.run_until_stalled(next_sme_req).map(|(req, stream)| {
            *next_sme_req = stream.into_future();
            req.expect("did not expect the SME request stream to end")
                .expect("error polling SME request stream")
        })
    }

    fn send_scan_results(exec: &mut fasync::Executor,
                         next_sme_req: &mut StreamFuture<ClientSmeRequestStream>,
                         ssids: &[&[u8]]) {
        let txn = match poll_sme_req(exec, next_sme_req) {
            Poll::Ready(ClientSmeRequest::Scan { txn, .. }) => txn,
            Poll::Pending => panic!("expected a request to be available"),
            _ => panic!("expected a Scan request"),
        };
        let txn = txn.into_stream().expect("failed to create a scan txn stream").control_handle();
        let mut results = Vec::new();
        for ssid in ssids {
            results.push(fidl_sme::EssInfo {
                best_bss: fidl_sme::BssInfo {
                    bssid: [0, 1, 2, 3, 4, 5],
                    ssid: ssid.to_vec(),
                    rx_dbm: -30,
                    channel: 1,
                    protected: true,
                    compatible: true,
                }
            });
        }
        txn.send_on_result(&mut results.iter_mut()).expect("failed to send scan results");
        txn.send_on_finished().expect("failed to send OnFinished to ScanTxn");
    }

    fn exchange_connect_with_sme(exec: &mut fasync::Executor,
                                 next_sme_req: &mut StreamFuture<ClientSmeRequestStream>,
                                 expected_ssid: &[u8],
                                 expected_password: &[u8],
                                 code: fidl_sme::ConnectResultCode) {
        let txn = expect_connect_req_to_sme(exec, next_sme_req, expected_ssid, expected_password);
        txn.control_handle().send_on_finished(code)
            .expect("failed to send OnFinished to ConnectTxn");
    }

    fn expect_connect_req_to_sme(exec: &mut fasync::Executor,
                                 next_sme_req: &mut StreamFuture<ClientSmeRequestStream>,
                                 expected_ssid: &[u8],
                                 expected_password: &[u8])
        -> fidl_sme::ConnectTransactionRequestStream
    {
        match poll_sme_req(exec, next_sme_req) {
            Poll::Ready(ClientSmeRequest::Connect { req, txn, .. }) => {
                assert_eq!(expected_ssid, &req.ssid[..]);
                assert_eq!(expected_password, &req.password[..]);
                txn.expect("expected a Connect transaction channel")
                    .into_stream()
                    .expect("failed to create a connect txn stream")
            },
            _ => panic!("expected a Connect request"),
        }
    }

    fn exchange_disconnect_with_sme(exec: &mut fasync::Executor,
                                    next_sme_req: &mut StreamFuture<ClientSmeRequestStream>) {
        let responder = expect_disconnect_req_to_sme(exec, next_sme_req);
        responder.send().expect("failed to respond to Disconnect request");
    }

    fn expect_disconnect_req_to_sme(exec: &mut fasync::Executor,
                                    next_sme_req: &mut StreamFuture<ClientSmeRequestStream>)
        -> fidl_sme::ClientSmeDisconnectResponder
    {
        match poll_sme_req(exec, next_sme_req) {
            Poll::Ready(ClientSmeRequest::Disconnect { responder }) => responder,
            _ => panic!("expected a Disconnect request"),
        }
    }

    fn create_ess_store(path: &Path) -> Arc<KnownEssStore> {
        Arc::new(KnownEssStore::new_with_paths(path.join("store.json"), path.join("store.json.tmp"))
            .expect("failed to create an KnownEssStore"))
    }

    fn create_client(ess_store: Arc<KnownEssStore>)
        -> (Client, impl Future<Output = ()>, ClientSmeRequestStream)
    {
        let (proxy, server) = create_proxy::<fidl_sme::ClientSmeMarker>()
            .expect("failed to create an sme channel");
        let (client, fut) = new_client(0, proxy, Arc::clone(&ess_store));
        let server = server.into_stream().expect("failed to create a request stream");
        (client, fut, server)
    }

}
