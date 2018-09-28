// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::{task, try_ready};
use futures::{Poll, Future};
use std::io;
use std::marker::Unpin;
use std::pin::PinMut;
use std::net::{self, SocketAddr};
use std::ops::Deref;

use crate::net::{set_nonblock, EventedFd};
use std::os::unix::io::AsRawFd;

/// An I/O object representing a UDP socket.
pub struct UdpSocket(EventedFd<net::UdpSocket>);

impl Deref for UdpSocket {
    type Target = EventedFd<net::UdpSocket>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl UdpSocket {
    pub fn bind(addr: &SocketAddr) -> io::Result<UdpSocket> {
        let socket = net::UdpSocket::bind(addr)?;
        UdpSocket::from_socket(socket)
    }

    pub fn from_socket(socket: net::UdpSocket) -> io::Result<UdpSocket> {
        set_nonblock(socket.as_raw_fd())?;

        unsafe { Ok(UdpSocket(EventedFd::new(socket)?)) }
    }

    pub fn recv_from<'a>(&'a self, buf: &'a mut [u8]) -> RecvFrom<'a> {
        RecvFrom { socket: self, buf }
    }

    pub fn async_recv_from(
        &self, buf: &mut [u8], cx: &mut task::Context,
    ) -> Poll<io::Result<(usize, SocketAddr)>> {
        try_ready!(EventedFd::poll_readable(&self.0, cx));
        match self.0.as_ref().recv_from(buf) {
            Err(e) => {
                if e.kind() == io::ErrorKind::WouldBlock {
                    self.0.need_read(cx);
                    Poll::Pending
                } else {
                    Poll::Ready(Err(e))
                }
            }
            Ok((size, addr)) => Poll::Ready(Ok((size, addr))),
        }
    }

    pub fn send_to<'a>(&'a self, buf: &'a [u8], addr: SocketAddr) -> SendTo<'a> {
        SendTo { socket: self, buf, addr }
    }

    pub fn async_send_to(
        &self, buf: &[u8], addr: SocketAddr, cx: &mut task::Context,
    ) -> Poll<io::Result<()>> {
        try_ready!(EventedFd::poll_writable(&self.0, cx));
        match self.0.as_ref().send_to(buf, addr) {
            Err(e) => {
                if e.kind() == io::ErrorKind::WouldBlock {
                    self.0.need_write(cx);
                    Poll::Pending
                } else {
                    Poll::Ready(Err(e))
                }
            }
            Ok(_) => Poll::Ready(Ok(())),
        }
    }
}

pub struct RecvFrom<'a> {
    socket: &'a UdpSocket,
    buf: &'a mut [u8],
}

impl<'a> Unpin for RecvFrom<'a> {}

impl<'a> Future for RecvFrom<'a> {
    type Output = io::Result<(usize, SocketAddr)>;

    fn poll(mut self: PinMut<Self>, cx: &mut task::Context)
        -> Poll<Self::Output>
    {
        let this = &mut *self;
        let (received, addr) = try_ready!(this.socket.async_recv_from(this.buf, cx));
        Poll::Ready(Ok((received, addr)))
    }
}

pub struct SendTo<'a> {
    socket: &'a UdpSocket,
    buf: &'a [u8],
    addr: SocketAddr,
}

impl<'a> Unpin for SendTo<'a> {}

impl<'a> Future for SendTo<'a> {
    type Output = io::Result<()>;

    fn poll(self: PinMut<Self>, cx: &mut task::Context)
        -> Poll<Self::Output>
    {
        self.socket.async_send_to(self.buf, self.addr, cx)
    }
}

#[cfg(test)]
mod tests {
    use crate::Executor;
    use super::UdpSocket;
    use std::io::Error;

    #[test]
    fn send_recv() {
        let mut exec = Executor::new().expect("could not create executor");

        let addr = "127.0.0.1:29995".parse().unwrap();
        let buf = b"hello world";
        let socket = UdpSocket::bind(&addr).expect("could not create socket");
        let fut = async move {
            await!(socket.send_to(buf, addr))?;
            let mut recvbuf = vec![0; 11];
            let (received, sender) = await!(socket.recv_from(&mut *recvbuf))?;
            assert_eq!(addr, sender);
            assert_eq!(received, buf.len());
            assert_eq!(&*buf, &*recvbuf);
            Ok::<(), Error>(())
        };

        exec.run_singlethreaded(fut).expect("failed to run udp socket test");
    }
}
