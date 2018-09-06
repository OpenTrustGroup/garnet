// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An implementation of a client for a fidl interface.

use {
    crate::{
        encoding2::{
            Encodable,
            Decodable,
            Encoder,
            Decoder,
            decode_transaction_header,
            TransactionHeader,
            TransactionMessage
        },
        Error,
    },
    fuchsia_async::{
        self as fasync,
        temp::{TempFutureExt, Either},
    },
    fuchsia_zircon as zx,
    futures::{
        future::{self, Future, Ready, AndThen, TryFutureExt},
        ready,
        stream::Stream,
        task::{self, Poll, Waker},
    },
    parking_lot::Mutex,
    slab::Slab,
    std::{
        collections::VecDeque,
        marker::Unpin,
        mem::{self, PinMut},
        ops::Deref,
        sync::Arc,
    },
};

/// Decode a new value of a decodable type from a transaction.
fn decode_transaction_body<D: Decodable>(mut buf: zx::MessageBuf) -> Result<D, Error> {
    let (bytes, handles) = buf.split_mut();
    let header_len = <TransactionHeader as Decodable>::inline_size();
    if bytes.len() < header_len { return Err(Error::OutOfRange); }
    let (_header_bytes, body_bytes) = bytes.split_at(header_len);

    let mut output = D::new_empty();
    Decoder::decode_into(body_bytes, handles, &mut output)?;
    Ok(output)
}

fn decode_transaction_body_fut<D: Decodable>(buf: zx::MessageBuf) -> Ready<Result<D, Error>> {
    future::ready(decode_transaction_body(buf))
}

/// A FIDL client which can be used to send buffers and receive responses via a channel.
#[derive(Debug, Clone)]
pub struct Client {
    inner: Arc<ClientInner>,
}


/// A future representing the raw response to a FIDL query.
pub type RawQueryResponseFut = Either<Ready<Result<zx::MessageBuf, Error>>, MessageResponse>;

/// A future representing the decoded response to a FIDL query.
pub type QueryResponseFut<D> =
    AndThen<
        RawQueryResponseFut,
        Ready<Result<D, Error>>,
        fn(zx::MessageBuf) -> Ready<Result<D, Error>>>;

/// A FIDL transaction id. Will not be zero for a message that includes a response.
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub struct Txid(u32);
/// A message interest id.
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
struct InterestId(usize);

impl InterestId {
    fn from_txid(txid: Txid) -> Self {
        InterestId(txid.0 as usize - 1)
    }

    fn as_raw_id(&self) -> usize {
        self.0
    }
}

impl Txid {
    fn from_interest_id(int_id: InterestId) -> Self {
        Txid((int_id.0 + 1) as u32)
    }

    fn as_raw_id(&self) -> u32 {
        self.0
    }
}

impl Client {
    /// Create a new client.
    ///
    /// `channel` is the asynchronous channel over which data is sent and received.
    /// `event_ordinals` are the ordinals on which events will be received.
    pub fn new(channel: fasync::Channel) -> Client {
        Client {
            inner: Arc::new(ClientInner {
                channel: channel,
                message_interests: Mutex::new(Slab::<MessageInterest>::new()),
                event_channel: Mutex::<EventChannel>::default(),
            })
        }
    }

    /// Retrieve the stream of event messages for the `Client`.
    /// Panics if the stream was already taken.
    pub fn take_event_receiver(&self) -> EventReceiver {
        {
            let mut lock = self.inner.event_channel.lock();

            if let EventListener::None = lock.listener {
                lock.listener = EventListener::New;
            } else {
                panic!("Event stream was already taken");
            }
        }

        EventReceiver {
            inner: self.inner.clone(),
        }
    }

    /// Send an encodable message without expecting a response.
    pub fn send<T: Encodable>(&self, msg: &mut T, ordinal: u32) -> Result<(), Error> {
        let (buf, handles) = (&mut vec![], &mut vec![]);
        let msg = &mut TransactionMessage {
            header: TransactionHeader {
                tx_id: 0,
                flags: 0,
                ordinal,
            },
            body: msg,
        };
        Encoder::encode(buf, handles, msg)?;
        self.send_raw_msg(&**buf, handles)
    }

    /// Send an encodable query and receive a decodable response.
    pub fn send_query<E: Encodable, D: Decodable>(&self, msg: &mut E, ordinal: u32)
        -> QueryResponseFut<D>
    {
        let (buf, handles) = (&mut vec![], &mut vec![]);
        let res_fut = self.send_raw_query(|tx_id| {
            let msg = &mut TransactionMessage {
                header: TransactionHeader {
                    tx_id: tx_id.as_raw_id(),
                    flags: 0,
                    ordinal,
                },
                body: msg,
            };
            Encoder::encode(buf, handles, msg)?;
            Ok((buf, handles))
        });

        res_fut.and_then(decode_transaction_body_fut::<D>)
    }

    /// Send a raw message without expecting a response.
    pub fn send_raw_msg(&self, buf: &[u8], handles: &mut Vec<zx::Handle>) -> Result<(), Error> {
        Ok(self.inner.channel.write(buf, handles).map_err(Error::ClientWrite)?)
    }

    /// Send a raw query and receive a response future.
    pub fn send_raw_query<'a, F>(&'a self, msg_from_id: F)
            -> RawQueryResponseFut
            where F: FnOnce(Txid) -> Result<(&'a mut [u8], &'a mut Vec<zx::Handle>), Error>
    {
        let id = self.inner.register_msg_interest();
        let (out_buf, handles) = match msg_from_id(Txid::from_interest_id(id)) {
            Ok(x) => x,
            Err(e) => return future::ready(Err(e)).left_future(),
        };
        if let Err(e) = self.inner.channel.write(out_buf, handles) {
            return future::ready(Err(Error::ClientWrite(e))).left_future();
        }

        MessageResponse {
            id: Txid::from_interest_id(id),
            client: Some(self.inner.clone()),
        }.right_future()
    }
}

#[must_use]
/// A future which polls for the response to a client message.
#[derive(Debug)]
pub struct MessageResponse {
    id: Txid,
    // `None` if the message response has been recieved
    client: Option<Arc<ClientInner>>,
}

impl Unpin for MessageResponse {}

impl Future for MessageResponse {
    type Output = Result<zx::MessageBuf, Error>;
    fn poll(mut self: PinMut<Self>, cx: &mut task::Context) -> Poll<Self::Output> {
        let this = &mut *self;
        let res;
        {
            let client = this.client.as_ref().ok_or(Error::PollAfterCompletion)?;

            res = client.poll_recv_msg_response(this.id, cx);
        }

        // Drop the client reference if the response has been received
        if let Poll::Ready(Ok(_)) = res {
            let client = this.client.take()
                .expect("MessageResponse polled after completion");
            client.wake_any();
        }

        res
    }
}

impl Drop for MessageResponse {
    fn drop(&mut self) {
        if let Some(client) = &self.client {
            client.deregister_msg_interest(InterestId::from_txid(self.id));
            client.wake_any();
        }
    }
}


/// An enum reprenting either a resolved message interest or a task on which to alert
/// that a response message has arrived.
#[derive(Debug)]
enum MessageInterest {
    /// A new `MessageInterest`
    WillPoll,
    /// A task is waiting to receive a response, and can be awoken with `Waker`.
    Waiting(Waker),
    /// A message has been received, and a task will poll to receive it.
    Received(zx::MessageBuf),
    /// A message has not been received, but the person interested in the response
    /// no longer cares about it, so the message should be discared upon arrival.
    Discard,
}

impl MessageInterest {
    /// Check if a message has been received.
    fn is_received(&self) -> bool {
        if let MessageInterest::Received(_) = *self {
            true
        } else {
            false
        }
    }

    fn unwrap_received(self) -> zx::MessageBuf {
        if let MessageInterest::Received(buf) = self {
            buf
        } else {
            panic!("EXPECTED received message")
        }
    }
}

/// A stream of events as `MessageBuf`s.
#[derive(Debug)]
pub struct EventReceiver {
    inner: Arc<ClientInner>,
}

impl Unpin for EventReceiver {}

impl Stream for EventReceiver {
    type Item = Result<zx::MessageBuf, Error>;

    fn poll_next(self: PinMut<Self>, cx: &mut task::Context) -> Poll<Option<Self::Item>> {
        Poll::Ready(match ready!(self.inner.poll_recv_event(cx)) {
            Ok(x) => Some(Ok(x)),
            Err(Error::ClientRead(zx::Status::PEER_CLOSED)) => None,
            Err(e) => Some(Err(e)),
        })
    }
}

impl Drop for EventReceiver {
    fn drop(&mut self) {
        self.inner.event_channel.lock().listener = EventListener::None;
        self.inner.wake_any();
    }
}

#[derive(Debug, Default)]
struct EventChannel {
    listener: EventListener,
    queue: VecDeque<zx::MessageBuf>,
}

#[derive(Debug)]
enum EventListener {
    /// No one is listening for the event
    None,
    /// Someone is listening for the event but has not yet polled
    New,
    /// Someone is listening for the event and can be woken via the `Waker`
    Some(Waker),
}

impl Default for EventListener {
    fn default() -> Self { EventListener::None }
}

/// A shared client channel which tracks EXPECTED and received responses
#[derive(Debug)]
struct ClientInner {
    channel: fasync::Channel,

    /// A map of message interests to either `None` (no message received yet)
    /// or `Some(DecodeBuf)` when a message has been received.
    /// An interest is registered with `register_msg_interest` and deregistered
    /// by either receiving a message via a call to `poll_recv` or manually
    /// deregistering with `deregister_msg_interest`
    message_interests: Mutex<Slab<MessageInterest>>,

    /// A queue of received events and a waker for the task to receive them.
    event_channel: Mutex<EventChannel>,
}

impl Deref for Client {
    type Target = fasync::Channel;

    fn deref(&self) -> &Self::Target {
        &self.inner.channel
    }
}

impl ClientInner {
    /// Registers interest in a response message.
    ///
    /// This function returns a `usize` ID which should be used to send a message
    /// via the channel. Responses are then received using `poll_recv`.
    fn register_msg_interest(&self) -> InterestId {
        // TODO(cramertj) use `try_from` here and assert that the conversion from
        // `usize` to `u32` hasn't overflowed.
        InterestId(self.message_interests.lock().insert(
            MessageInterest::WillPoll))
    }

    fn poll_recv_event(
        &self,
        cx: &mut task::Context,
    ) -> Poll<Result<zx::MessageBuf, Error>> {
        let is_closed = self.recv_all(cx)?;

        let mut lock = self.event_channel.lock();

        if let Some(msg_buf) = lock.queue.pop_front() {
            Poll::Ready(Ok(msg_buf))
        } else {
            lock.listener = EventListener::Some(cx.waker().clone());
            if is_closed {
                Poll::Ready(Err(Error::ClientRead(zx::Status::PEER_CLOSED)))
            } else {
                Poll::Pending
            }
        }
    }

    fn poll_recv_msg_response(
        &self,
        txid: Txid,
        cx: &mut task::Context,
    ) -> Poll<Result<zx::MessageBuf, Error>> {
        let is_closed = self.recv_all(cx)?;

        let mut message_interests = self.message_interests.lock();
        let interest_id = InterestId::from_txid(txid);
        if message_interests.get(interest_id.as_raw_id())
            .expect("Polled unregistered interest")
            .is_received()
        {
            // If, by happy accident, we just raced to getting the result,
            // then yay! Return success.
            let buf = message_interests.remove(interest_id.as_raw_id()).unwrap_received();
            Poll::Ready(Ok(buf))
        } else {
            // Set the current waker to be notified when a response arrives.
            *message_interests.get_mut(interest_id.as_raw_id())
                .expect("Polled unregistered interest") =
                    MessageInterest::Waiting(cx.waker().clone());

            if is_closed {
                Poll::Ready(Err(Error::ClientRead(zx::Status::PEER_CLOSED)))
            } else {
                Poll::Pending
            }
        }
    }

    /// Poll for the receipt of any response message or an event.
    ///
    /// Returns whether or not the channel is closed.
    fn recv_all(
        &self,
        cx: &mut task::Context,
    ) -> Result<bool, Error> {
        // TODO(cramertj) return errors if one has occured _ever_ in recv_all, not just if
        // one happens on this call.

        loop {
            let mut buf = zx::MessageBuf::new();
            match self.channel.recv_from(&mut buf, cx) {
                Poll::Ready(Ok(())) => {}
                Poll::Ready(Err(zx::Status::PEER_CLOSED)) => return Ok(true),
                Poll::Ready(Err(e)) => return Err(Error::ClientRead(e)),
                Poll::Pending => return Ok(false),
            }

            let (header, _) = decode_transaction_header(buf.bytes()).map_err(|_| Error::InvalidHeader)?;
            if header.tx_id == 0 { // received an event
                let mut lock = self.event_channel.lock();
                lock.queue.push_back(buf);
                if let EventListener::Some(ref waker) = lock.listener { waker.wake(); }
            } else { // received a message response
                let recvd_interest_id = InterestId::from_txid(Txid(header.tx_id));

                // Look for a message interest with the given ID.
                // If one is found, store the message so that it can be picked up later.
                let mut message_interests = self.message_interests.lock();
                let raw_recvd_interest_id = recvd_interest_id.as_raw_id();
                if let Some(&MessageInterest::Discard) =
                    message_interests.get(raw_recvd_interest_id)
                {
                    message_interests.remove(raw_recvd_interest_id);
                } else if let Some(entry) = message_interests.get_mut(raw_recvd_interest_id) {
                    let old_entry = mem::replace(entry, MessageInterest::Received(buf));
                    if let MessageInterest::Waiting(waker) = old_entry {
                        // Wake up the task to let them know a message has arrived.
                        waker.wake();
                    }
                }
            }
        }
    }

    fn deregister_msg_interest(&self, InterestId(id): InterestId) {
        let mut lock = self.message_interests.lock();
        if lock[id].is_received() {
            lock.remove(id);
        } else {
            lock[id] = MessageInterest::Discard;
        }
    }

    // Wakes up an arbitrary task that has begun polling on the channel so that
    // it will call recv_all and be registered as the new channel reader.
    fn wake_any(&self) {
        // Try to wake up message interests first, rather than the event listener.
        // The event listener is a stream, and so could be between poll_nexts,
        // blocked on a message interest completing before it begins polling again.
        // Waking up only the event listener in these cases would cause a deadlock
        // since the channel wouldn't be read from by the awoken event listener,
        // so the message interest the event listener was blocked on would never
        // complete.
        //
        // Message interests, however, should always be actively polled once
        // they've begun being polled on a task.
        {
            let lock = self.message_interests.lock();
            for (_, message_interest) in lock.iter() {
                if let MessageInterest::Waiting(waker) = message_interest {
                    waker.wake();
                    return
                }
            }
        }
        {
            let lock = self.event_channel.lock();
            if let EventListener::Some(waker) = &lock.listener {
                waker.wake();
                return;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        futures::{FutureExt, StreamExt},
        fuchsia_async::TimeoutExt,
        fuchsia_zircon::DurationNum,
        std::io,
    };

    #[test]
    fn client() {
        const EXPECTED: &[u8] = &[
            0, 0, 0, 0, 0, 0, 0, 0, // 32 bit tx_id followed by 32 bits of padding
            0, 0, 0, 0, // 32 bits for flags
            42, 0, 0, 0, // 32 bit ordinal
            55, // 8 bit data
            0, 0, 0, 0, 0, 0, 0, // 7 bytes of padding after our 1 byte of data
        ];

        let mut executor = fasync::Executor::new().unwrap();

        let (client_end, server_end) = zx::Channel::create().unwrap();
        let client_end = fasync::Channel::from_channel(client_end).unwrap();
        let client = Client::new(client_end);

        let server = fasync::Channel::from_channel(server_end).unwrap();
        let mut buffer = zx::MessageBuf::new();
        let receiver = server.recv_msg(&mut buffer).map_ok(|(_chan, buf)| {
            assert_eq!(EXPECTED, buf.bytes());
        });

        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = receiver.on_timeout(
            300.millis().after_now(),
            || panic!("did not receive message in time!"));

        let sender = fasync::Timer::new(100.millis().after_now()).map(|()|{
            client.send(&mut 55u8, 42).unwrap();
            Ok(())
        });

        let done = receiver.try_join(sender);
        executor.run_singlethreaded(done).unwrap();
    }

    #[test]
    fn client_with_response() {
        const EXPECTED: &[u8] = &[
            1, 0, 0, 0, 0, 0, 0, 0, // 32 bit tx_id followed by 32 bits of padding
            0, 0, 0, 0, // 32 bits for flags
            42, 0, 0, 0, // 32 bit ordinal
            55, // 8 bit data
            0, 0, 0, 0, 0, 0, 0, // 7 bytes of padding after our 1 byte of data
        ];

        let mut executor = fasync::Executor::new().unwrap();

        let (client_end, server_end) = zx::Channel::create().unwrap();
        let client_end = fasync::Channel::from_channel(client_end).unwrap();
        let client = Client::new(client_end);

        let server = fasync::Channel::from_channel(server_end).unwrap();
        let mut buffer = zx::MessageBuf::new();
        let receiver = server.recv_msg(&mut buffer).map_ok(|(chan, buf)| {
            assert_eq!(EXPECTED, buf.bytes());
            let id = 1; // internally, the first slot in a slab returns a `0`. We then add one
                        // since FIDL txids start with `1`.

            let response = &mut TransactionMessage {
                header: TransactionHeader {
                    tx_id: id,
                    flags: 0,
                    ordinal: 42,
                },
                body: &mut 55,
            };

            let (bytes, handles) = (&mut vec![], &mut vec![]);
            Encoder::encode(bytes, handles, response).expect("Encoding failure");
            chan.write(bytes, handles).expect("Server channel write failed");
        });

        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = receiver.on_timeout(
            300.millis().after_now(),
            || panic!("did not receiver message in time!"
        ));

        let sender = client.send_query::<u8, u8>(&mut 55, 42)
            .map_ok(|x|assert_eq!(x, 55))
            .map_err(|e| {
                io::Error::new(
                    io::ErrorKind::Other,
                    &*format!("fidl error: {:?}", e))
            });

        // add a timeout to receiver so if test is broken it doesn't take forever
        let sender = sender.on_timeout(
            300.millis().after_now(),
            || panic!("did not receive response in time!")
        ).map(Ok);

        let done = receiver.try_join(sender);
        executor.run_singlethreaded(done).unwrap();
    }

    #[test]
    #[should_panic]
    fn event_cant_be_taken_twice() {
        let _exec = fasync::Executor::new().unwrap();
        let (client_end, _) = zx::Channel::create().unwrap();
        let client_end = fasync::Channel::from_channel(client_end).unwrap();
        let client = Client::new(client_end);
        let _foo = client.take_event_receiver();
        client.take_event_receiver();
    }

    #[test]
    fn event_can_be_taken() {
        let _exec = fasync::Executor::new().unwrap();
        let (client_end, _) = zx::Channel::create().unwrap();
        let client_end = fasync::Channel::from_channel(client_end).unwrap();
        let client = Client::new(client_end);
        client.take_event_receiver();
    }

    #[test]
    fn event_received() {
        let mut executor = fasync::Executor::new().unwrap();

        let (client_end, server_end) = zx::Channel::create().unwrap();
        let client_end = fasync::Channel::from_channel(client_end).unwrap();
        let client = Client::new(client_end);

        // Send the event from the server
        let server = fasync::Channel::from_channel(server_end).unwrap();
        let event = &mut TransactionMessage {
            header: TransactionHeader {
                tx_id: 0,
                flags: 0,
                ordinal: 5,
            },
            body: &mut 55i32,
        };
        let (bytes, handles) = (&mut vec![], &mut vec![]);
        Encoder::encode(bytes, handles, event).expect("Encoding failure");
        server.write(bytes, handles).expect("Server channel write failed");
        drop(server);

        let recv = client.take_event_receiver()
            .into_future()
            .then(|(x, stream)| {
                let x = x.expect("should contain one element");
                let x = x.expect("fidl error");
                let x: i32 = decode_transaction_body(x).expect("failed to decode event");
                assert_eq!(x, 55);
                stream.into_future()
            })
            .map(|(x, _stream)| assert!(x.is_none(), "should have emptied"));

        // add a timeout to receiver so if test is broken it doesn't take forever
        let recv = recv.on_timeout(
            300.millis().after_now(),
            || panic!("did not receive event in time!")
        );

        executor.run_singlethreaded(recv);
    }
}
