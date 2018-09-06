// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MlmeEvent};
use futures::channel::mpsc;

use crate::{MlmeRequest, Ssid};
use crate::sink::MlmeSink;

const DEFAULT_BEACON_PERIOD: u32 = 100;
const DEFAULT_DTIM_PERIOD: u32 = 1;

// A token is an opaque value that identifies a particular request from a user.
// To avoid parameterizing over many different token types, we introduce a helper
// trait that enables us to group them into a single generic parameter.
pub trait Tokens {
    type StartToken;
    type StopToken;
}

#[derive(Clone, Debug, PartialEq)]
pub struct Config {
    pub ssid: Ssid,
    pub channel: u8
}

mod internal {
    pub type UserSink<T> = crate::sink::UnboundedSink<super::UserEvent<T>>;
}
use self::internal::*;

pub type UserStream<T> = mpsc::UnboundedReceiver<UserEvent<T>>;

enum State {
    Idle,
    Started {
        ssid: Ssid,
    }
}

pub struct ApSme<T: Tokens> {
    state: State,
    mlme_sink: MlmeSink,
    user_sink: UserSink<T>,
}

#[derive(Debug)]
pub enum StartResult {
    Success,
    AlreadyStarted,
    InternalError,
}

// A message from the Ap to a user or a group of listeners
#[derive(Debug)]
pub enum UserEvent<T: Tokens> {
    StartComplete {
        token: T::StartToken,
        result: StartResult,
    },
    StopComplete {
        token: T::StopToken,
    }
}

impl<T: Tokens> ApSme<T> {
    pub fn new() -> (Self, crate::MlmeStream, UserStream<T>) {
        let (mlme_sink, mlme_stream) = mpsc::unbounded();
        let (user_sink, user_stream) = mpsc::unbounded();
        let sme = ApSme {
            state: State::Idle,
            mlme_sink: MlmeSink::new(mlme_sink),
            user_sink: UserSink::new(user_sink),
        };
        (sme, mlme_stream, user_stream)
    }

    pub fn on_start_command(&mut self, config: Config, token: T::StartToken) {
        match self.state {
            State::Idle => {
                let req = create_start_request(&config);
                self.mlme_sink.send(MlmeRequest::StartAp(req));
                // Currently, MLME doesn't send any response back. We simply assume
                // that the start request succeeded immediately
                self.user_sink.send(UserEvent::StartComplete {
                    token,
                    result: StartResult::Success,
                });
                self.state = State::Started { ssid: config.ssid };
            },
            State::Started { .. } => {
                let result = StartResult::AlreadyStarted;
                self.user_sink.send(UserEvent::StartComplete { token, result });
            }
        }
    }

    pub fn on_stop_command(&mut self, token: T::StopToken) {
        match &self.state {
            State::Idle => {
                self.user_sink.send(UserEvent::StopComplete { token });
            },
            State::Started { ssid } => {
                let req = fidl_mlme::StopRequest {
                    ssid: String::from_utf8_lossy(&ssid).to_string(),
                };
                self.mlme_sink.send(MlmeRequest::StopAp(req));
                // Currently, MLME doesn't send any response back. We simply assume
                // that the stop request succeeded immediately
                self.user_sink.send(UserEvent::StopComplete { token });
                self.state = State::Idle;
            }
        }
    }
}

impl<T: Tokens> super::Station for ApSme<T> {
    fn on_mlme_event(&mut self, _event: MlmeEvent) {
        // The MLME driver currently doesn't send any events
    }
}

fn create_start_request(config: &Config) -> fidl_mlme::StartRequest {
    fidl_mlme::StartRequest {
        ssid: String::from_utf8_lossy(&config.ssid).to_string(),
        bss_type: fidl_mlme::BssTypes::Infrastructure,
        beacon_period: DEFAULT_BEACON_PERIOD,
        dtim_period: DEFAULT_DTIM_PERIOD,
        channel: config.channel,
        rsne: None,
    }
}
