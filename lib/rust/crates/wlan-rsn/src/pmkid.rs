// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bytes::Bytes;
use crate::Error;
use failure::{self, bail, ensure};

pub type Pmkid = Bytes;

pub fn new(pmkid: Bytes) -> Result<Pmkid, failure::Error> {
    ensure!(pmkid.len() == 16, Error::InvalidPmkidLength(pmkid.len()));
    Ok(pmkid)
}
