// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A futures-rs executor design specifically for Fuchsia OS.

#![deny(warnings)]
#![deny(missing_docs)]

extern crate bytes;
extern crate crossbeam;
#[macro_use]
extern crate futures;
extern crate fuchsia_zircon as zx;
extern crate libc;
extern crate net2;
extern crate parking_lot;
extern crate slab;

/// A future which can be used by multiple threads at once.
pub mod atomic_future;

mod channel;
pub use channel::{Channel, RecvMsg};
mod on_signals;
pub use on_signals::OnSignals;
mod rwhandle;
pub use rwhandle::RWHandle;
mod socket;
pub use socket::Socket;
mod timer;
pub use timer::{Interval, Timer, TimeoutExt, OnTimeout};
mod executor;
pub use executor::{Executor, EHandle, spawn};
mod fifo;
pub use fifo::{Fifo, FifoEntry};
pub mod net;

#[macro_export]
macro_rules! many_futures {
    ($future:ident, [$first:ident, $($subfuture:ident $(,)*)*]) => {

        enum $future<$first, $($subfuture,)*> {
            $first($first),
            $(
                $subfuture($subfuture),
            )*
        }

        impl<$first, $($subfuture,)*> Future for $future<$first, $($subfuture,)*>
        where
            $first: Future,
            $(
                $subfuture: Future<Item = $first::Item, Error = $first::Error>,
            )*
        {
            type Item = $first::Item;
            type Error = $first::Error;
            fn poll(&mut self, cx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
                match self {
                    $future::$first(x) => x.poll(cx),
                    $(
                        $future::$subfuture(x) => x.poll(cx),
                    )*
                }
            }
        }
    }
}
