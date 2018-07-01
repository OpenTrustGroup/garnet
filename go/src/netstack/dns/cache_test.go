// Copyright 2017 The Netstack Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package dns

import (
	"testing"
	"time"

	"netstack/dns/dnsmessage"
)

func makeResourceHeader(name string, ttl uint32) dnsmessage.ResourceHeader {
	return dnsmessage.ResourceHeader{
		Name:  name,
		Type:  dnsmessage.TypeA,
		Class: dnsmessage.ClassINET,
		TTL:   ttl,
	}
}

func makeQuestion(name string) *dnsmessage.Question {
	return &dnsmessage.Question{
		Name:  name,
		Type:  dnsmessage.TypeA,
		Class: dnsmessage.ClassINET,
	}
}

var smallTestResources = []dnsmessage.Resource{
	&dnsmessage.AResource{
		ResourceHeader: makeResourceHeader("example.com.", 5),
		A:              [4]byte{127, 0, 0, 1},
	},
	&dnsmessage.AResource{
		ResourceHeader: makeResourceHeader("example.com.", 5),
		A:              [4]byte{127, 0, 0, 2},
	},
}

var smallTestQuestion = makeQuestion("example.com.")

var soaAuthority = dnsmessage.SOAResource{
	ResourceHeader: makeResourceHeader("example.com.", 5),
	MinTTL:         12,
}

// Tests a simple insert and lookup pair.
func TestLookup(t *testing.T) {
	cache := newCache()
	cache.insertAll(smallTestResources)
	rrs := cache.lookup(smallTestQuestion)
	if len(rrs) != 2 {
		t.Errorf("cache.lookup failed. Got %d. Want %d.", len(rrs), 2)
	}
	for _, rr := range rrs {
		if rr.Header().Name != "example.com." {
			t.Errorf("cache.lookup failed. Got '%q'. Want 'example.com.'", rr.Header().Name)
		}
	}
}

// Tests that entries are pruned when they expire, and not before.
func TestExpires(t *testing.T) {
	cache := newCache()

	// These records expire at 5 seconds.
	testTime := time.Now()
	testHookNow = func() time.Time { return testTime }
	cache.insertAll(smallTestResources)

	// Still there after t=4 seconds.
	testTime = testTime.Add(4 * time.Second)
	cache.prune()
	rrs := cache.lookup(smallTestQuestion)
	if len(rrs) != 2 {
		t.Errorf("cache.prune failed. Got %d. Want %d.", len(rrs), 2)
	}

	// Gone after t=6 seconds.
	testTime = testTime.Add(2 * time.Second)
	cache.prune()
	rrs = cache.lookup(smallTestQuestion)
	if len(rrs) != 0 {
		t.Errorf("cache.prune failed. Got %d. Want %d.", len(rrs), 0)
	}
}

// Tests that we can't insert more than maxEntries entries, but after pruning old ones, we can insert again.
func TestMaxEntries(t *testing.T) {
	cache := newCache()

	testTime := time.Now()
	testHookNow = func() time.Time { return testTime }

	// One record that expires at 10 seconds.
	cache.insertAll([]dnsmessage.Resource{
		&dnsmessage.AResource{
			ResourceHeader: makeResourceHeader("example.com.", 10),
			A:              [4]byte{127, 0, 0, 1},
		},
	})

	// A bunch that expire at 5 seconds.
	for i := 0; i < maxEntries; i++ {
		cache.insertAll([]dnsmessage.Resource{
			&dnsmessage.AResource{
				ResourceHeader: makeResourceHeader("example.com.", 5),
				A:              [4]byte{byte(i >> 24), byte(i >> 16), byte(i >> 8), byte(i)},
			},
		})
	}

	rrs := cache.lookup(smallTestQuestion)
	if len(rrs) != maxEntries {
		t.Errorf("cache.insertAll failed. Got %d. Want %d.", len(rrs), maxEntries)
	}

	// Cache is at capacity. Can't insert anymore.
	cache.insertAll([]dnsmessage.Resource{
		&dnsmessage.AResource{
			ResourceHeader: makeResourceHeader("foo.example.com.", 5),
			A:              [4]byte{192, 168, 0, 1},
		},
	})
	rrs = cache.lookup(makeQuestion("foo.example.com."))
	if len(rrs) != 0 {
		t.Errorf("cache.insertAll failed. Got %d. Want %d.", len(rrs), 0)
	}

	// Advance the clock so the 5 second entries expire. Insert should succeed.
	testTime = testTime.Add(6 * time.Second)
	cache.insertAll([]dnsmessage.Resource{
		&dnsmessage.AResource{
			ResourceHeader: makeResourceHeader("foo.example.com.", 5),
			A:              [4]byte{192, 168, 0, 1},
		},
	})

	rrs = cache.lookup(makeQuestion("foo.example.com."))
	if len(rrs) != 1 {
		t.Errorf("cache.insertAll failed. Got %d. Want %d.", len(rrs), 1)
	}

	rrs = cache.lookup(makeQuestion("example.com."))
	if len(rrs) != 1 {
		t.Errorf("cache.insertAll failed. Got %d. Want %d.", len(rrs), 1)
	}
}

// Tests that we get results when looking up a domain alias.
func TestCNAME(t *testing.T) {
	cache := newCache()
	cache.insertAll(smallTestResources)

	// One CNAME record that points at an existing record.
	cache.insertAll([]dnsmessage.Resource{
		&dnsmessage.CNAMEResource{
			ResourceHeader: makeResourceHeader("foobar.com.", 10),
			CNAME:          "example.com.",
		},
	})

	rrs := cache.lookup(makeQuestion("foobar.com."))
	if len(rrs) != 2 {
		t.Errorf("cache.lookup failed. Got %d. Want %d.", len(rrs), 2)
	}
	for _, rr := range rrs {
		if rr.Header().Name != "example.com." {
			t.Errorf("cache.lookup failed. Got '%q'. Want 'example.com.'", rr.Header().Name)
		}
	}
}

// Tests that the cache doesn't store multiple identical records.
func TestDupe(t *testing.T) {
	cache := newCache()
	cache.insertAll(smallTestResources)
	cache.insertAll(smallTestResources)
	rrs := cache.lookup(smallTestQuestion)
	if len(rrs) != 2 {
		t.Errorf("cache.lookup failed. Got %d. Want %d.", len(rrs), 2)
	}
}

// Tests that we can insert and expire negative resources.
func TestNegative(t *testing.T) {
	cache := newCache()

	// The negative record expires at 12 seconds (taken from the SOA authority resource).
	testTime := time.Now()
	testHookNow = func() time.Time { return testTime }
	cache.insertNegative(smallTestQuestion, &dnsmessage.Message{
		Questions:   []dnsmessage.Question{*smallTestQuestion},
		Authorities: []dnsmessage.Resource{&soaAuthority},
	})

	// Still there after t=11 seconds.
	testTime = testTime.Add(11 * time.Second)
	cache.prune()
	rrs := cache.lookup(smallTestQuestion)
	if len(rrs) != 1 {
		t.Errorf("cache.prune failed. Got %d. Want %d.", len(rrs), 1)
	}

	// Gone after t=13 seconds.
	testTime = testTime.Add(2 * time.Second)
	cache.prune()
	rrs = cache.lookup(smallTestQuestion)
	if len(rrs) != 0 {
		t.Errorf("cache.prune failed. Got %d. Want %d.", len(rrs), 0)
	}
}

// Tests that a negative resource is replaced when we have an actual resource for that query.
func TestNegativeUpdate(t *testing.T) {
	cache := newCache()
	cache.insertNegative(smallTestQuestion, &dnsmessage.Message{
		Questions:   []dnsmessage.Question{*smallTestQuestion},
		Authorities: []dnsmessage.Resource{&soaAuthority},
	})
	cache.insertAll(smallTestResources)
	rrs := cache.lookup(smallTestQuestion)
	if len(rrs) != 2 {
		t.Errorf("cache.lookup failed. Got %s. Want %d.", debugString(rrs), 2)
	}
}
