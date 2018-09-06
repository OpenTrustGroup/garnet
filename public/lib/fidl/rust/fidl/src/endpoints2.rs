// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Wrapper types for the endpoints of a connection.

use {
    crate::Error,
    fuchsia_async as fasync,
    fuchsia_zircon as zx,
    std::marker::PhantomData,
};

/// A marker for a particular FIDL service.
///
/// Implementations of this trait can be used to manufacture instances of a FIDL service
/// and get metadata about a particular service.
pub trait ServiceMarker: Sized {
    /// The type of the structure against which FIDL requests are made.
    /// Queries made against the proxy are sent to the paired `ServerEnd`.
    type Proxy: Proxy;

    /// The type of the stream of requests coming into a server.
    type RequestStream: RequestStream;

    /// The name of the service (to be used for service lookup and discovery).
    const NAME: &'static str;
}

/// A type which allows querying a remote FIDL server over a channel.
pub trait Proxy: Sized {
    /// Create a proxy over the given channel.
    fn from_channel(inner: fasync::Channel) -> Self;
}

/// A stream of requests coming into a FIDL server over a channel.
pub trait RequestStream: Sized {
    /// A type that can be used to send events and shut down the request stream.
    type ControlHandle;

    /// Returns a copy of the `ControlHandle` for the given stream.
    /// This handle can be used to send events and shut down the request stream.
    fn control_handle(&self) -> Self::ControlHandle;

    /// Create a request stream from the given channel.
    fn from_channel(inner: fasync::Channel) -> Self;
}

/// The `Client` end of a FIDL connection.
#[derive(Eq, PartialEq, Hash)]
pub struct ClientEnd<T> {
    inner: zx::Channel,
    phantom: PhantomData<T>,
}

impl<T> ClientEnd<T> {
    /// Create a new client from the provided channel.
    pub fn new(inner: zx::Channel) -> Self {
        ClientEnd { inner, phantom: PhantomData }
    }

    /// Extract the inner channel.
    pub fn into_channel(self) -> zx::Channel {
        self.inner
    }
}

impl<T: ServiceMarker> ClientEnd<T> {
    /// Convert the `ClientEnd` into a `Proxy` through which FIDL calls may be made.
    pub fn into_proxy(self) -> Result<T::Proxy, Error> {
        Ok(T::Proxy::from_channel(
            fasync::Channel::from_channel(self.inner)
                .map_err(Error::AsyncChannel)?))
    }
}

impl<T> zx::AsHandleRef for ClientEnd<T> {
    fn as_handle_ref(&self) -> zx::HandleRef {
        self.inner.as_handle_ref()
    }
}

impl<T> From<ClientEnd<T>> for zx::Handle {
    fn from(client: ClientEnd<T>) -> zx::Handle {
        client.into_channel().into()
    }
}

impl<T> From<zx::Handle> for ClientEnd<T> {
    fn from(handle: zx::Handle) -> Self {
        ClientEnd {
            inner: handle.into(),
            phantom: PhantomData,
        }
    }
}

impl<T> From<zx::Channel> for ClientEnd<T> {
    fn from(chan: zx::Channel) -> Self {
        ClientEnd {
            inner: chan,
            phantom: PhantomData,
        }
    }
}

impl<T: ServiceMarker> ::std::fmt::Debug for ClientEnd<T> {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        write!(f, "ClientEnd(name={}, channel={:?})", T::NAME, self.inner)
    }
}

impl<T> zx::HandleBased for ClientEnd<T> {}

/// The `Server` end of a FIDL connection.
#[derive(Eq, PartialEq, Hash)]
pub struct ServerEnd<T> {
    inner: zx::Channel,
    phantom: PhantomData<T>,
}

impl<T> ServerEnd<T> {
    /// Create a new `ServerEnd` from the provided channel.
    pub fn new(inner: zx::Channel) -> ServerEnd<T> {
        ServerEnd { inner, phantom: PhantomData }
    }

    /// Extract the inner channel.
    pub fn into_channel(self) -> zx::Channel {
        self.inner
    }

    /// Create a stream of requests off of the channel.
    pub fn into_stream(self) -> Result<T::RequestStream, Error>
        where T: ServiceMarker,
    {
        Ok(T::RequestStream::from_channel(
            fasync::Channel::from_channel(self.inner)
                .map_err(Error::AsyncChannel)?))
    }

    /// Create a stream of requests and an event-sending handle
    /// from the channel.
    pub fn into_stream_and_control_handle(self)
        -> Result<(T::RequestStream, <T::RequestStream as RequestStream>::ControlHandle), Error>
        where T: ServiceMarker,
    {
        let stream = self.into_stream()?;
        let control_handle = stream.control_handle();
        Ok((stream, control_handle))
    }
}

impl<T> zx::AsHandleRef for ServerEnd<T> {
    fn as_handle_ref(&self) -> zx::HandleRef {
        self.inner.as_handle_ref()
    }
}

impl<T> From<ServerEnd<T>> for zx::Handle {
    fn from(server: ServerEnd<T>) -> zx::Handle {
        server.into_channel().into()
    }
}

impl<T> From<zx::Handle> for ServerEnd<T> {
    fn from(handle: zx::Handle) -> Self {
        ServerEnd {
            inner: handle.into(),
            phantom: PhantomData,
        }
    }
}

impl<T> From<zx::Channel> for ServerEnd<T> {
    fn from(chan: zx::Channel) -> Self {
        ServerEnd {
            inner: chan,
            phantom: PhantomData,
        }
    }
}

impl<T: ServiceMarker> ::std::fmt::Debug for ServerEnd<T> {
    fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        write!(f, "ServerEnd(name={}, channel={:?})", T::NAME, self.inner)
    }
}

impl<T> zx::HandleBased for ServerEnd<T> {}

handle_based_codable![ClientEnd :- <T,>, ServerEnd :- <T,>,];

/// Create a server endpoint and a client proxy connected to it by a channel.
///
/// Useful for sending channel handles to calls that take arguments
/// of type `request<SomeInterface>`
pub fn create_endpoints<T: ServiceMarker>() -> Result<(T::Proxy, ServerEnd<T>), Error> {
    let (client, server) = zx::Channel::create().map_err(Error::ChannelPairCreate)?;
    let proxy = ClientEnd::<T>::new(client).into_proxy()?;
    let server_end = ServerEnd::new(server);
    Ok((proxy, server_end))
}
