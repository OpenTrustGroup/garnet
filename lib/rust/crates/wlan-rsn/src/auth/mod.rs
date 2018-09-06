// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod psk;

use self::psk::Psk;
use failure;
use crate::rsna::{SecAssocResult, VerifiedKeyFrame};

#[derive(Debug, PartialEq)]
pub enum Method {
    Psk(psk::Psk),
}

impl Method {
    pub fn from_config(cfg: Config) -> Result<Method, failure::Error> {
        match cfg {
            Config::Psk(c) => Ok(Method::Psk(Psk{config: c})),
        }
    }

    pub fn on_eapol_key_frame(&self, _frame: VerifiedKeyFrame) -> SecAssocResult {
        match self {
            // None of the supported authentication methods requires EAPOL frame exchange.
            _ => Ok(vec![]),
        }
    }
}

#[derive(Debug)]
pub enum Config {
    Psk(psk::Config),
}

impl Config {
    pub fn for_psk(passphrase: &[u8], ssid: &[u8]) -> Result<Config, failure::Error> {
        psk::Config::new(passphrase, ssid)
            .map_err(|e| e.into())
            .map(|c| Config::Psk(c))
    }
}
