// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(deprecated)] // Necessary for AsciiExt usage from clap args_enum macro

use clap::{_clap_count_exprs, arg_enum};
use fidl_fuchsia_wlan_device as wlan;
use structopt::StructOpt;

arg_enum!{
    #[derive(PartialEq, Copy, Clone, Debug)]
    pub enum RoleArg {
        Client,
        Ap
    }
}

impl ::std::convert::From<RoleArg> for wlan::MacRole {
    fn from(arg: RoleArg) -> Self {
        match arg {
            RoleArg::Client => wlan::MacRole::Client,
            RoleArg::Ap => wlan::MacRole::Ap,
        }
    }
}

#[derive(StructOpt, Debug)]
pub enum Opt {
    #[structopt(name = "phy")]
    /// commands for wlan phy devices
    Phy(PhyCmd),

    #[structopt(name = "iface")]
    /// commands for wlan iface devices
    Iface(IfaceCmd),

    #[structopt(name = "client")]
    /// commands for client stations
    Client(ClientCmd),

    #[structopt(name = "ap")]
    /// commands for AP stations
    Ap(ApCmd),
}

#[derive(StructOpt, Copy, Clone, Debug)]
pub enum PhyCmd {
    #[structopt(name = "list")]
    /// lists phy devices
    List,
    #[structopt(name = "query")]
    /// queries a phy device
    Query {
        #[structopt(raw(required = "true"))]
        /// id of the phy to query
        phy_id: u16,
    },
}

#[derive(StructOpt, Copy, Clone, Debug)]
pub enum IfaceCmd {
    #[structopt(name = "new")]
    /// creates a new iface device
    New {
        #[structopt(short = "p", long = "phy", raw(required = "true"))]
        /// id of the phy that will host the iface
        phy_id: u16,

        #[structopt(short = "r", long = "role", raw(possible_values = "&RoleArg::variants()"),
                    default_value = "Client", raw(case_insensitive = "true"))]
        /// role of the new iface
        role: RoleArg,
    },

    #[structopt(name = "del")]
    /// destroys an iface device
    Delete {
        #[structopt(short = "p", long = "phy", raw(required = "true"))]
        /// id of the phy that hosts the iface
        phy_id: u16,

        #[structopt(raw(required = "true"))]
        /// iface id to destroy
        iface_id: u16,
    },

    #[structopt(name = "list")]
    List,
    #[structopt(name = "stats")]
    Stats {
        iface_id: Option<u16>,
    },
}

#[derive(StructOpt, Clone, Debug)]
pub enum ClientCmd {
    #[structopt(name = "scan")]
    Scan {
        #[structopt(raw(required = "true"))]
        iface_id: u16
    },
    #[structopt(name = "connect")]
    Connect {
        #[structopt(raw(required = "true"))]
        iface_id: u16,
        #[structopt(raw(required = "true"))]
        ssid: String,
        #[structopt(short = "p", long = "password", help = "WPA2 PSK")]
        password: Option<String>,
        #[structopt(short = "y", long = "phy", help = "Choose one from (hr, erp, ht, vht, hew) to specify an upper bound")]
        phy_str: Option<String>,
        #[structopt(short = "w", long = "cbw", help = "Choose one from (20, 40, 80, 160, 80p80) to specify an upper bound")]
        cbw_str: Option<String>,
    },
    #[structopt(name = "disconnect")]
    Disconnect {
        #[structopt(raw(required = "true"))]
        iface_id: u16,
    },
    #[structopt(name = "status")]
    Status {
        #[structopt(raw(required = "true"))]
        iface_id: u16,
    }
}

#[derive(StructOpt, Clone, Debug)]
pub enum ApCmd {
    #[structopt(name = "start")]
    Start {
        #[structopt(raw(required = "true"))]
        iface_id: u16,
        #[structopt(short = "s", long = "ssid")]
        ssid: String,
        #[structopt(short = "p", long = "password")]
        password: Option<String>,
        #[structopt(short = "c", long = "channel")]
        channel: u8,
    },
    #[structopt(name = "stop")]
    Stop {
        #[structopt(raw(required = "true"))]
        iface_id: u16,
    },
}

