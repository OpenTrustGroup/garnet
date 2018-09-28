// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::Never;

use {
    futures::{
        future::FutureObj,
        prelude::*,
        ready,
    },
    std::{
        marker::Unpin,
        pin::PinMut,
    },
};

pub struct State<E>(FutureObj<'static, Result<State<E>, E>>);

pub struct StateMachine<E>{
    cur_state: State<E>
}

impl<E> Unpin for StateMachine<E> {}

impl<E> Future for StateMachine<E> {
    type Output = Result<Never, E>;

    fn poll(mut self: PinMut<Self>, cx: &mut task::Context) -> Poll<Self::Output> {
        loop {
            match ready!(self.cur_state.0.poll_unpin(cx)) {
                Ok(next) => self.cur_state = next,
                Err(e) => return Poll::Ready(Err(e)),
            }
        }
    }
}

pub trait IntoStateExt<E>: Future<Output = Result<State<E>, E>> {
    fn into_state(self) -> State<E>
        where Self: Sized + Send + 'static
    {
        State(FutureObj::new(Box::new(self)))
    }

    fn into_state_machine(self) -> StateMachine<E>
        where Self: Sized + Send + 'static
    {
        StateMachine {
            cur_state: self.into_state()
        }
    }
}

impl<F, E> IntoStateExt<E> for F where F: Future<Output = Result<State<E>, E>> {}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        fuchsia_async as fasync,
        futures::channel::mpsc,
        std::mem,
    };

    #[test]
    fn state_machine() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (sender, receiver) = mpsc::unbounded();
        let mut state_machine = sum_state(0, receiver).into_state_machine();

        let r = exec.run_until_stalled(&mut state_machine);
        assert_eq!(Poll::Pending, r);

        sender.unbounded_send(2).unwrap();
        sender.unbounded_send(3).unwrap();
        mem::drop(sender);

        let r = exec.run_until_stalled(&mut state_machine);
        assert_eq!(Poll::Ready(Err(5)), r);
    }

    async fn sum_state(current: u32, stream: mpsc::UnboundedReceiver<u32>)
        -> Result<State<u32>, u32>
    {
        let (number, stream) = await!(stream.into_future());
        match number {
            Some(number) => Ok(make_sum_state(current + number, stream)),
            None => Err(current),
        }
    }

    // A workaround for the "recursive impl Trait" problem in the compiler
    fn make_sum_state(current: u32, stream: mpsc::UnboundedReceiver<u32>) -> State<u32> {
        sum_state(current, stream).into_state()
    }
}
