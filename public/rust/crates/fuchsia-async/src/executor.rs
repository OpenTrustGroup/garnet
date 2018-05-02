// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crossbeam::sync::SegQueue;
use futures::{Async, Future, FutureExt, Never, task};
use futures::executor::{Executor as FutureExecutor, SpawnError};
use parking_lot::{Mutex, Condvar};
use slab::Slab;
use zx;

use atomic_future::AtomicFuture;

use std::cell::RefCell;
use std::fmt;
use std::sync::{Arc, Weak};
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread;
use std::{usize, u64};

const EMPTY_WAKEUP_ID: u64 = u64::MAX;
const TASK_READY_WAKEUP_ID: u64 = u64::MAX - 1;

/// Spawn a new task to be run on the global executor.
pub fn spawn<F>(future: F)
    where F: Future<Item = (), Error = Never> + Send + 'static
{
    Inner::spawn(&EHandle::local().inner, Box::new(future));
}

/// A trait for handling the arrival of a packet on a `zx::Port`.
///
/// This trait should be implemented by users who wish to write their own
/// types which receive asynchronous notifications from a `zx::Port`.
/// Implementors of this trait generally contain a `futures::task::AtomicTask` which
/// is used to wake up the task which can make progress due to the arrival of
/// the packet.
///
/// `PacketReceiver`s should be registered with a `Core` using the
/// `register_receiver` method on `Core`, `Handle`, or `Remote`.
/// Upon registration, users will receive a `ReceiverRegistration`
/// which provides `key` and `port` methods. These methods can be used to wait on
/// asynchronous signals.
pub trait PacketReceiver: Send + Sync + 'static {
    /// Receive a packet when one arrives.
    fn receive_packet(&self, packet: zx::Packet);
}

/// A registration of a `PacketReceiver`.
/// When dropped, it will automatically deregister the `PacketReceiver`.
// NOTE: purposefully does not implement `Clone`.
#[derive(Debug)]
pub struct ReceiverRegistration<T: PacketReceiver> {
    receiver: Arc<T>,
    ehandle: EHandle,
    key: u64,
}

impl<T> ReceiverRegistration<T> where T: PacketReceiver {
    /// The key with which `Packet`s destined for this receiver should be sent on the `zx::Port`.
    pub fn key(&self) -> u64 {
        self.key
    }

    /// The internal `PacketReceiver`.
    pub fn receiver(&self) -> &T {
        &*self.receiver
    }

    /// The `zx::Port` on which packets destined for this `PacketReceiver` should be queued.
    pub fn port(&self) -> &zx::Port {
        self.ehandle.port()
    }
}

impl<T> Drop for ReceiverRegistration<T> where T: PacketReceiver {
    fn drop(&mut self) {
        self.ehandle.deregister_receiver(self.key);
    }
}

/// A port-based executor for Fuchsia OS.
// NOTE: intentionally does not implement `Clone`.
pub struct Executor {
    inner: Arc<Inner>,
}

impl fmt::Debug for Executor {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.debug_struct("Executor")
         .field("port", &self.inner.port)
         .finish()
    }
}

thread_local!(
    static EXECUTOR: RefCell<Option<Weak<Inner>>> = RefCell::new(None)
);

impl Executor {
    /// Creates a new executor.
    pub fn new() -> Result<Self, zx::Status> {
        let executor = Executor {
            inner: Arc::new(Inner {
                port: zx::Port::create()?,
                done: AtomicBool::new(false),
                threads: Mutex::new(Vec::new()),
                receivers: Mutex::new(Slab::new()),
                ready_tasks: SegQueue::new(),
            })
        };

        executor.ehandle().set_local();

        Ok(executor)
    }

    /// Returns a handle to the executor.
    pub fn ehandle(&self) -> EHandle {
        EHandle {
            inner: self.inner.clone()
        }
    }

    /// Run a single future to completion on a single thread.
    // Takes `&mut self` to ensure that only one thread-manager is running at a time.
    pub fn run_singlethreaded<F>(&mut self, mut main_future: F) -> Result<F::Item, F::Error>
        where F: Future
    {
        let local_map = &mut task::LocalMap::new();

        // Since there are no other threads running, we don't have to use the EMPTY_WAKEUP_ID,
        // so instead we save it for use as the main task wakeup id.
        struct MainTaskWake(Arc<Inner>);
        impl task::Wake for MainTaskWake {
            fn wake(arc_self: &Arc<Self>) {
                arc_self.0.notify_empty();
            }
        }
        let waker = task::Waker::from(Arc::new(MainTaskWake(self.inner.clone())));

        let executor = EHandle { inner: self.inner.clone() };
        let executor_one = &mut &executor;
        let executor_two = &mut &executor;

        let cx = &mut task::Context::new(local_map, &waker, executor_one);

        let mut res = main_future.poll(cx);

        loop {
            if let Async::Ready(res) = res? {
                return Ok(res);
            }
            res = Ok(Async::Pending);

            let packet = match self.inner.port.wait(zx::Time::INFINITE) {
                Ok(packet) => packet,
                Err(status) => {
                    panic!("Error calling port wait: {:?}", status);
                }
            };

            match packet.key() {
                EMPTY_WAKEUP_ID => {
                    res = main_future.poll(cx);
                }
                TASK_READY_WAKEUP_ID => {
                    // TODO: loop but don't starve
                    if let Some(task) = self.inner.ready_tasks.try_pop() {
                        task.future.try_poll(&task.clone().into(), executor_two);
                    }
                }
                receiver_key => {
                    self.inner.deliver_packet(receiver_key as usize, packet);
                }
            }
        }
    }

    /// Run a single future to completion using multiple threads.
    // Takes `&mut self` to ensure that only one thread-manager is running at a time.
    pub fn run<F>(&mut self, future: F, num_threads: usize)
        -> Result<F::Item, F::Error>
        where F: Future + Send + 'static,
              Result<F::Item, F::Error>: Send + 'static,
    {
        let pair = Arc::new((Mutex::new(None), Condvar::new()));
        let pair2 = pair.clone();

        // Spawn a future which will set the result upon completion.
        Inner::spawn(&self.inner, Box::new(future.then(move |fut_result| {
            let (lock, cvar) = &*pair2;
            let mut result = lock.lock();
            *result = Some(fut_result);
            cvar.notify_one();
            Ok(())
        })));

        // Start worker threads
        self.inner.done.store(false, Ordering::SeqCst);
        self.create_worker_threads(num_threads);

        // Wait until the signal the future has completed.
        let (lock, cvar) = &*pair;
        let mut result = lock.lock();
        while result.is_none() {
            cvar.wait(&mut result);
        }

        // Spin down worker threads
        self.inner.done.store(true, Ordering::SeqCst);
        self.join_all();

        // Unwrap is fine because of the check to `is_none` above.
        result.take().unwrap()
    }

    /// Add `num_workers` worker threads to the executor's thread pool.
    fn create_worker_threads(&self, num_workers: usize) {
        let mut threads = self.inner.threads.lock();
        for _ in 0..num_workers {
            threads.push(self.new_worker());
        }
    }

    fn join_all(&self) {
        let mut threads = self.inner.threads.lock();

        // Send a user packet to wake up all the threads
        for _thread in threads.iter() {
            self.inner.notify_empty();
        }

        // Join the worker threads
        for thread in threads.drain(..) {
            thread.join().expect("Couldn't join worker thread.");
        }
    }

    fn new_worker(&self) -> thread::JoinHandle<()> {
        let inner = self.inner.clone();
        thread::spawn(move || Self::worker_lifecycle(inner))
    }

    fn worker_lifecycle(inner: Arc<Inner>) {
        let mut executor: EHandle = EHandle { inner: inner.clone() };
        executor.clone().set_local();
        loop {
            if inner.done.load(Ordering::SeqCst) {
                return;
            }

            let packet = match inner.port.wait(zx::Time::INFINITE) {
                Ok(packet) => packet,
                Err(status) => {
                    // TODO: logging, awaken main thread, signal error somehow.
                    // Maybe retry on a timeout?
                    eprintln!("Error calling port wait: {:?}", status);
                    return;
                }
            };

            match packet.key() {
                EMPTY_WAKEUP_ID => {}
                TASK_READY_WAKEUP_ID => {
                    // TODO: loop but don't starve
                    if let Some(task) = inner.ready_tasks.try_pop() {
                        task.future.try_poll(&task.clone().into(), &mut executor);
                    }
                }
                receiver_key => {
                    inner.deliver_packet(receiver_key as usize, packet);
                }
            }
        }
    }
}

impl Drop for Executor {
    fn drop(&mut self) {
        self.inner.done.store(true, Ordering::SeqCst);

        // Wake the threads so they can kill themselves.
        self.join_all();

        // Drop all of the packet receivers
        self.inner.receivers.lock().clear();

        // Drop all of the uncompleted tasks
        while let Some(_) = self.inner.ready_tasks.try_pop() {}
    }
}

/// A handle to an executor.
#[derive(Clone)]
pub struct EHandle {
    inner: Arc<Inner>,
}

impl fmt::Debug for EHandle {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.debug_struct("EHandle")
         .field("port", &self.inner.port)
         .finish()
    }
}

impl FutureExecutor for EHandle {
    fn spawn(&mut self, f: Box<Future<Item = (), Error = Never> + Send>) -> Result<(), SpawnError> {
        <&EHandle>::spawn(&mut &*self, f)
    }
}

impl<'a> FutureExecutor for &'a EHandle {
    fn spawn(&mut self, f: Box<Future<Item = (), Error = Never> + Send>) -> Result<(), SpawnError> {
        Inner::spawn(&self.inner, f);
        Ok(())
    }
}

impl EHandle {
    /// Returns the thread-local executor.
    pub fn local() -> Self {
        let inner = EXECUTOR.with(|e| e.borrow().as_ref().and_then(|e| e.upgrade()))
            .expect("Fuchsia Executor must be created first");

        EHandle { inner }
    }

    fn set_local(self) {
        let weak_inner = Arc::downgrade(&self.inner);
        EXECUTOR.with(|e| *e.borrow_mut() = Some(weak_inner));
    }

    /// Get a reference to the Fuchsia `zx::Port` being used to listen for events.
    pub fn port(&self) -> &zx::Port {
        &self.inner.port
    }

    /// Registers a `PacketReceiver` with the executor and returns a registration.
    /// The `PacketReceiver` will be deregistered when the `Registration` is dropped.
    pub fn register_receiver<T>(&self, receiver: Arc<T>) -> ReceiverRegistration<T>
        where T: PacketReceiver
    {
        let key = self.inner.receivers.lock().insert(receiver.clone()) as u64;

        ReceiverRegistration {
            ehandle: self.clone(),
            key,
            receiver,
        }
    }

    fn deregister_receiver(&self, key: u64) {
        self.inner.receivers.lock().remove(key as usize);
    }
}

struct Inner {
    port: zx::Port,
    done: AtomicBool,
    threads: Mutex<Vec<thread::JoinHandle<()>>>,
    receivers: Mutex<Slab<Arc<PacketReceiver>>>,
    ready_tasks: SegQueue<Arc<Task>>,
}

impl Inner {
    fn spawn(arc_self: &Arc<Self>, future: Box<Future<Item = (), Error = Never> + Send>) {
        let task = Arc::new(Task {
            future: AtomicFuture::new(future, task::LocalMap::new()),
            executor: arc_self.clone(),
        });

        arc_self.ready_tasks.push(task);
        arc_self.notify_task_ready();
    }

    fn notify_task_ready(&self) {
        // TODO: optimize so that this function doesn't push new items onto
        // the queue if all worker threads are already awake
        self.notify_id(TASK_READY_WAKEUP_ID);
    }

    fn notify_empty(&self) {
        self.notify_id(EMPTY_WAKEUP_ID);
    }

    fn notify_id(&self, id: u64) {
        let up = zx::UserPacket::from_u8_array([0; 32]);
        let packet = zx::Packet::from_user_packet(
            id, 0 /* status??? */, up);
        if let Err(e) = self.port.queue(&packet) {
            // TODO: logging
            eprintln!("Failed to queue notify in port: {:?}", e);
        }
    }

    fn deliver_packet(&self, key: usize, packet: zx::Packet) {
        let receiver = match self.receivers.lock().get(key) {
            // Clone the `Arc` so that we don't hold the lock
            // any longer than absolutely necessary.
            // The `receive_packet` impl may be arbitrarily complex.
            Some(receiver) => receiver.clone(),
            None => return,
        };
        receiver.receive_packet(packet);
    }
}

struct Task {
    future: AtomicFuture,
    executor: Arc<Inner>,
}

impl task::Wake for Task {
    fn wake(arc_self: &Arc<Self>) {
        arc_self.executor.ready_tasks.push(arc_self.clone());
        arc_self.executor.notify_task_ready();
    }
}
