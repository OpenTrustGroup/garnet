// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bytes::{Buf, BufMut};
use futures::io::{AsyncRead, AsyncWrite, Initializer};
use futures::task;
use futures::{Async, Future, Poll, Stream};
use libc;
use std::io::{self, Read, Write};
use std::net::{self, SocketAddr};
use std::ops::Deref;

use std::os::unix::io::AsRawFd;

use net2::{TcpBuilder, TcpStreamExt};

use net::{set_nonblock, EventedFd};

/// An I/O object representing a TCP socket listening for incoming connections.
///
/// This object can be converted into a stream of incoming connections for
/// various forms of processing.
pub struct TcpListener(EventedFd<net::TcpListener>);

impl Deref for TcpListener {
    type Target = EventedFd<net::TcpListener>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl TcpListener {
    pub fn bind(addr: &SocketAddr) -> io::Result<TcpListener> {
        let sock = match *addr {
            SocketAddr::V4(..) => TcpBuilder::new_v4(),
            SocketAddr::V6(..) => TcpBuilder::new_v6(),
        }?;

        sock.reuse_address(true)?;
        sock.bind(addr)?;
        let listener = sock.listen(1024)?;
        TcpListener::new(listener)
    }

    pub fn new(listener: net::TcpListener) -> io::Result<TcpListener> {
        set_nonblock(listener.as_raw_fd())?;

        unsafe { Ok(TcpListener(EventedFd::new(listener)?)) }
    }

    pub fn accept(self) -> Acceptor {
        Acceptor(Some(self))
    }

    pub fn accept_stream(self) -> AcceptStream {
        AcceptStream(self)
    }

    pub fn async_accept(
        &mut self, cx: &mut task::Context,
    ) -> Poll<(TcpStream, SocketAddr), io::Error> {
        try_ready!(EventedFd::poll_readable(&self.0, cx));

        match self.0.as_ref().accept() {
            Err(e) => {
                if e.kind() == io::ErrorKind::WouldBlock {
                    self.0.need_read(cx);
                    return Ok(Async::Pending);
                }
                return Err(e);
            }
            Ok((sock, addr)) => {
                return TcpStream::from_stream(sock).map(|sock| Async::Ready((sock, addr)));
            }
        }
    }

    pub fn from_listener(
        listener: net::TcpListener, _addr: &SocketAddr,
    ) -> io::Result<TcpListener> {
        TcpListener::new(listener)
    }
}

pub struct Acceptor(Option<TcpListener>);

impl Future for Acceptor {
    type Item = (TcpListener, TcpStream, SocketAddr);
    type Error = io::Error;

    fn poll(&mut self, cx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        let (stream, addr);
        {
            let listener = self.0
                .as_mut()
                .expect("polled an Acceptor after completion");
            let (s, a) = try_ready!(listener.async_accept(cx));
            stream = s;
            addr = a;
        }
        let listener = self.0.take().unwrap();
        Ok(Async::Ready((listener, stream, addr)))
    }
}

pub struct AcceptStream(TcpListener);

impl Stream for AcceptStream {
    type Item = (TcpStream, SocketAddr);
    type Error = io::Error;

    fn poll_next(&mut self, cx: &mut task::Context) -> Poll<Option<Self::Item>, Self::Error> {
        let (stream, addr) = try_ready!(self.0.async_accept(cx));
        Ok(Async::Ready(Some((stream, addr))))
    }
}

enum ConnectState {
    // The stream has not yet connected to an address.
    New,
    // `connect` has been called on the stream but it has not yet completed.
    Connecting,
    // `connect` has succeeded. Subsequent attempts to connect will fail.
    // state.
    Connected,
}

pub struct TcpStream {
    state: ConnectState,
    stream: EventedFd<net::TcpStream>,
}

impl Deref for TcpStream {
    type Target = EventedFd<net::TcpStream>;

    fn deref(&self) -> &Self::Target {
        &self.stream
    }
}

impl TcpStream {
    pub fn connect(addr: SocketAddr) -> io::Result<Connector> {
        let sock = match addr {
            SocketAddr::V4(..) => TcpBuilder::new_v4(),
            SocketAddr::V6(..) => TcpBuilder::new_v6(),
        }?;

        let stream = sock.to_tcp_stream()?;
        set_nonblock(stream.as_raw_fd())?;
        // This is safe because the file descriptor for stream will live as long as the TcpStream.
        let stream = unsafe { EventedFd::new(stream)? };
        let stream = TcpStream {
            state: ConnectState::New,
            stream,
        };

        Ok(Connector {
            addr,
            stream: Some(stream),
        })
    }

    pub fn async_connect(
        &mut self, addr: &SocketAddr, cx: &mut task::Context,
    ) -> Poll<(), io::Error> {
        match self.state {
            ConnectState::New => match self.stream.as_ref().connect(addr) {
                Err(e) => {
                    if e.raw_os_error() == Some(libc::EINPROGRESS) {
                        self.state = ConnectState::Connecting;
                        self.stream.need_write(cx);
                        Ok(Async::Pending)
                    } else {
                        Err(e)
                    }
                }
                Ok(()) => {
                    self.state = ConnectState::Connected;
                    Ok(Async::Ready(()))
                }
            },
            ConnectState::Connecting => self.stream.poll_writable(cx).map_err(Into::into),
            ConnectState::Connected => Err(io::Error::from_raw_os_error(libc::EISCONN)),
        }
    }

    pub fn read_buf<B: BufMut>(
        &self, buf: &mut B, cx: &mut task::Context,
    ) -> Poll<usize, io::Error> {
        match (&self.stream).as_ref().read(unsafe { buf.bytes_mut() }) {
            Ok(n) => {
                unsafe {
                    buf.advance_mut(n);
                }
                Ok(Async::Ready(n))
            }
            Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                self.stream.need_read(cx);
                Ok(Async::Pending)
            }
            Err(e) => Err(e),
        }
    }

    pub fn write_buf<B: Buf>(&self, buf: &mut B, cx: &mut task::Context) -> Poll<usize, io::Error> {
        match (&self.stream).as_ref().write(buf.bytes()) {
            Ok(n) => {
                buf.advance(n);
                Ok(Async::Ready(n))
            }
            Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                self.stream.need_write(cx);
                Ok(Async::Pending)
            }
            Err(e) => Err(e),
        }
    }

    fn from_stream(stream: net::TcpStream) -> io::Result<TcpStream> {
        set_nonblock(stream.as_raw_fd())?;

        // This is safe because the file descriptor for stream will live as long as the TcpStream.
        let stream = unsafe { EventedFd::new(stream)? };

        Ok(TcpStream {
            state: ConnectState::Connected,
            stream,
        })
    }
}

impl AsyncRead for TcpStream {
    unsafe fn initializer(&self) -> Initializer {
        // This is safe because `zx::Socket::read` does not examine
        // the buffer before reading into it.
        Initializer::nop()
    }

    fn poll_read(&mut self, cx: &mut task::Context, buf: &mut [u8]) -> Poll<usize, io::Error> {
        self.stream.poll_read(cx, buf).map_err(|e| e.into())
    }

    // TODO: override poll_vectored_read and call readv on the underlying stream
}

impl AsyncWrite for TcpStream {
    fn poll_write(&mut self, cx: &mut task::Context, buf: &[u8]) -> Poll<usize, io::Error> {
        self.stream.poll_write(cx, buf).map_err(|e| e.into())
    }

    fn poll_flush(&mut self, _: &mut task::Context) -> Poll<(), io::Error> {
        Ok(Async::Ready(()))
    }

    fn poll_close(&mut self, _: &mut task::Context) -> Poll<(), io::Error> {
        Ok(Async::Ready(()))
    }

    // TODO: override poll_vectored_write and call writev on the underlying stream
}

pub struct Connector {
    addr: SocketAddr,
    stream: Option<TcpStream>,
}

impl Future for Connector {
    type Item = TcpStream;
    type Error = io::Error;

    fn poll(&mut self, cx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        {
            let stream = self.stream
                .as_mut()
                .expect("polled a Connector after completion");
            try_ready!(stream.async_connect(&self.addr, cx));
        }
        let stream = self.stream.take().unwrap();
        Ok(Async::Ready(stream))
    }
}
