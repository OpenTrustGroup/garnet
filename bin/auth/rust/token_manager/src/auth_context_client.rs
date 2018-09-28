// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl::endpoints::{create_endpoints, ClientEnd};
use fidl_fuchsia_auth::{AuthenticationContextProviderMarker, AuthenticationContextProviderProxy,
                        AuthenticationUiContextMarker};

/// An object capable of acquiring new AuthenticationUiContexts.
pub struct AuthContextClient {
    /// A proxy for a AuthenticationContextProvider FIDL interface.
    provider_proxy: AuthenticationContextProviderProxy,
}

impl AuthContextClient {
    /// Creates a new AuthContextClient from the supplied ClientEnd.
    pub fn from_client_end(
        client_end: ClientEnd<AuthenticationContextProviderMarker>,
    ) -> Result<Self, Error> {
        Ok(AuthContextClient {
            provider_proxy: client_end.into_proxy()?,
        })
    }

    /// Creates a new authentication context and returns the ClientEnd for
    /// communicating with it.
    pub fn get_new_ui_context(&self) -> Result<ClientEnd<AuthenticationUiContextMarker>, Error> {
        let (client_end, server_end) = create_endpoints()?;

        self.provider_proxy
            .get_authentication_ui_context(server_end)
            .map(|_| client_end)
            .map_err(Error::from)
    }
}
