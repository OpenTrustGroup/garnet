// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async;
use fidl::Error;
use fidl::endpoints2::RequestStream;
use fidl_fuchsia_auth::{TokenManagerFactoryRequest, TokenManagerFactoryRequestStream};
use futures::future::{self, FutureResult};
use futures::prelude::*;

use super::manager::TokenManager;

/// A factory to create instances of the TokenManager for individual users.
pub struct TokenManagerFactory;

impl TokenManagerFactory {
    /// Creates a new TokenManagerFactory to handle requests from the supplied
    /// channel.
    pub fn spawn(chan: async::Channel) {
        async::spawn(
            TokenManagerFactoryRequestStream::from_channel(chan)
                .for_each(Self::handle_request)
                .map(|_| ())
                .recover(|e| warn!("Error running TokenManagerFactory {:?}", e)),
        )
    }

    /// Handles a single request to the TokenManagerFactory.
    fn handle_request(req: TokenManagerFactoryRequest) -> FutureResult<(), Error> {
        match req {
            TokenManagerFactoryRequest::GetTokenManager {
                user_id,
                application_url,
                auth_provider_configs,
                auth_context_provider,
                token_manager,
                ..
            } => {
                info!(
                    "Creating token manager for user {} and app {}",
                    user_id, application_url
                );
                TokenManager::spawn(
                    user_id,
                    application_url,
                    auth_provider_configs,
                    auth_context_provider,
                    token_manager,
                );
                future::ok::<(), Error>(())
            }
        }
    }
}
