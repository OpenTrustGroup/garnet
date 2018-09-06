// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

pub mod ap;
pub mod client;
mod sink;

use fidl_fuchsia_wlan_mlme as fidl_mlme;
use futures::channel::mpsc;
use std::collections::HashSet;

pub type Ssid = Vec<u8>;

pub struct DeviceInfo {
    pub supported_channels: HashSet<u8>,
    pub addr: [u8; 6],
}

#[derive(Debug)]
pub enum MlmeRequest {
    Scan(fidl_mlme::ScanRequest),
    Join(fidl_mlme::JoinRequest),
    Authenticate(fidl_mlme::AuthenticateRequest),
    Associate(fidl_mlme::AssociateRequest),
    Deauthenticate(fidl_mlme::DeauthenticateRequest),
    Eapol(fidl_mlme::EapolRequest),
    SetKeys(fidl_mlme::SetKeysRequest),
    StartAp(fidl_mlme::StartRequest),
    StopAp(fidl_mlme::StopRequest),
}

pub trait Station {
    fn on_mlme_event(&mut self, event: fidl_mlme::MlmeEvent);
}

pub type MlmeStream = mpsc::UnboundedReceiver<MlmeRequest>;
