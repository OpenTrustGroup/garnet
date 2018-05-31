// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate fidl_wlan_mlme as fidl_mlme;
extern crate fuchsia_zircon as zx;
extern crate futures;

pub mod client;
pub mod mlme;

use futures::channel::mpsc;

pub enum MlmeRequest {
    Scan(fidl_mlme::ScanRequest),
    Join(fidl_mlme::JoinRequest),
    Authenticate(fidl_mlme::AuthenticateRequest),
    Associate(fidl_mlme::AssociateRequest),
    Deauthenticate(fidl_mlme::DeauthenticateRequest),
}

pub trait Station {
    fn on_mlme_event(&mut self, event: fidl_mlme::MlmeEvent);
}

pub type MlmeStream = mpsc::UnboundedReceiver<MlmeRequest>;
