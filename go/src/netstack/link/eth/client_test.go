// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(tamird): move this to package eth_test when
// https://fuchsia-review.googlesource.com/c/garnet/+/181782 is submitted.
package eth

import "testing"

func TestClient_AllocForSend(t *testing.T) {
	arena, err := NewArena()
	if err != nil {
		t.Fatal(err)
	}

	c := Client{
		arena: arena,
		txDepth: 1,
	}

	if txDepthMin := 1; c.txDepth < txDepthMin {
		t.Fatalf("%s is a no-op when txDepth is less than %d", t.Name(), txDepthMin)
	}

	// Arrange for arena allocation to fail.
	arena.mu.freebufs = arena.mu.freebufs[:0]

	if got := c.AllocForSend(); got != nil {
		t.Fatalf("AllocForSend() = %v, want %v", got, nil)
	}

	// Arrange for arena allocation to succeed.
	arena.mu.freebufs = arena.mu.freebufs[:cap(arena.mu.freebufs)]

	// Saturate the client.
	for i := c.txDepth; i > 0; i--  {
		if got := c.AllocForSend(); got == nil {
			t.Fatalf("AllocForSend() = %v, want non-nil", got)
		}
	}

	if got := c.AllocForSend(); got != nil {
		t.Fatalf("AllocForSend() = %v, want %v", got, nil)
	}
}
