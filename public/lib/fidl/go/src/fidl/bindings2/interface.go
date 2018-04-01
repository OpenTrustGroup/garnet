// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings2

import (
	"fmt"
	"sync/atomic"
	"syscall/zx"
)

// ServiceRequest is an abstraction over a FIDL interface request which is
// intended to be used as part of service discovery.
type ServiceRequest interface {
	// Name returns the name of the service being requested.
	Name() string

	// Channel returns the underlying channel of the ServiceRequest.
	Channel() zx.Channel
}

// NewInterfaceRequest generates two sides of a channel with one layer of
// type casts out of the way to minimize the amount of generated code. Semantically,
// the two sides of the channel represent the interface request and the client
// side of the interface (the proxy). It returns an error on failure.
func NewInterfaceRequest() (zx.Channel, *Proxy, error) {
	h0, h1, err := zx.NewChannel(0)
	if err != nil {
		return zx.Channel(zx.HANDLE_INVALID), nil, err
	}
	return h0, &Proxy{Channel: h1}, nil
}

// Proxy represents the client side of a FIDL interface.
type Proxy struct {
	// Channel is the underlying channel endpoint for this interface.
	zx.Channel

	// Txid is a monotonically and atomically increasing transaction ID
	// used for FIDL messages.
	Txid uint32
}

// IsValid returns true if the underlying channel is a valid handle.
func (p Proxy) IsValid() bool {
	h := zx.Handle(p.Channel)
	return h.IsValid()
}

// Send sends the request payload over the channel with the specified ordinal
// without a response.
func (p *Proxy) Send(ordinal uint32, req Payload) error {
	// Allocate maximum size of a message on the stack.
	var respb [zx.ChannelMaxMessageBytes]byte
	var resph [zx.ChannelMaxMessageHandles]zx.Handle

	// Marshal the message into the buffer.
	header := MessageHeader{
		Txid:    0, // Txid == 0 for messages without a response.
		Ordinal: ordinal,
	}
	nb, nh, err := MarshalMessage(&header, req, respb[:], resph[:])
	if err != nil {
		return err
	}

	// Write the encoded bytes to the channel.
	return p.Channel.Write(respb[:nb], resph[:nh], 0)
}

// Recv waits for an event and writes the response into the response payload.
func (p *Proxy) Recv(ordinal uint32, resp Payload) error {
	// Allocate maximum size of a message on the stack.
	var respb [zx.ChannelMaxMessageBytes]byte
	var resph [zx.ChannelMaxMessageHandles]zx.Handle

	// Wait on the channel to be readable or close.
	h := zx.Handle(p.Channel)
	sigs, err := h.WaitOne(
		zx.SignalChannelReadable|zx.SignalChannelPeerClosed,
		zx.TimensecInfinite,
	)
	if err != nil {
		return err
	}
	// If it closed, let's just report that and stop here.
	if (sigs & zx.SignalChannelPeerClosed) != 0 {
		return &zx.Error{Status: zx.ErrPeerClosed}
	}
	// Otherwise, now we can read!
	nb, nh, err := p.Channel.Read(respb[:], resph[:], 0)
	if err != nil {
		return err
	}

	// Unmarshal the message.
	var header MessageHeader
	if err := UnmarshalMessage(respb[:nb], resph[:nh], &header, resp); err != nil {
		return err
	}
	if header.Ordinal != ordinal {
		return fmt.Errorf("expected ordinal %x, got %x", ordinal, header.Ordinal)
	}
	return nil
}

// Call sends the request payload over the channel with the specified ordinal
// and synchronously waits for a response. It then writes the response into the
// response payload.
func (p *Proxy) Call(ordinal uint32, req Payload, resp Payload) error {
	// Allocate maximum size of a message on the stack.
	var respb [zx.ChannelMaxMessageBytes]byte
	var resph [zx.ChannelMaxMessageHandles]zx.Handle

	// Make sure the Txid is non-zero, since that's reserved for messages without
	// a response.
	txid := atomic.AddUint32(&p.Txid, 1)
	for txid == 0 {
		txid = atomic.AddUint32(&p.Txid, 1)
	}

	// Marshal the message into the buffer
	header := MessageHeader{
		Txid:    txid,
		Ordinal: ordinal,
	}
	nb, nh, err := MarshalMessage(&header, req, respb[:], resph[:])
	if err != nil {
		return err
	}

	// Make the IPC call.
	cnb, cnh, _, err := p.Channel.Call(0, zx.TimensecInfinite, respb[:nb], resph[:nh], respb[:], resph[:])
	if err != nil {
		return err
	}

	// Unmarshal the message.
	if err := UnmarshalMessage(respb[:cnb], resph[:cnh], &header, resp); err != nil {
		return err
	}
	if header.Ordinal != ordinal {
		return fmt.Errorf("expected ordinal %x, got %x", ordinal, header.Ordinal)
	}
	if header.Txid != txid {
		return fmt.Errorf("expected txid %d, got %d", p.Txid, header.Txid)
	}
	return nil
}

// Stub represents a generated type which wraps the server-side implementation of a
// FIDL interface.
//
// It contains logic which is able to dispatch into the correct implementation given
// the incoming message ordinal and its data.
type Stub interface {
	// Dispatch dispatches into the appropriate method implementation for a FIDL
	// interface by using the ordinal.
	//
	// It also takes the data as bytes and transforms it into arguments usable by
	// the method implementation. It then optionally returns a response if the
	// method has a response.
	Dispatch(ordinal uint32, bytes []byte, handles []zx.Handle) (Payload, error)
}
