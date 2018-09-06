// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"app/context"
	"bytes"
	"crypto/rand"
	"fmt"
	"io/ioutil"
	"log"
	"math/big"
	"os"
	"os/exec"
	"strings"
	"syslog/logger"
	"testing"
	"time"
)

const (
	loglistener = "/system/bin/log_listener"
)

func TestFullStack(t *testing.T) {
	if _, err := os.Stat(loglistener); err != nil {
		t.Fatalf("error stating log listener: %s", err)
	}

	tag := genTag()

	ctx := context.CreateFromStartupInfo()
	if err := logger.InitDefaultLoggerWithTags(ctx.Connector(), tag); err != nil {
		t.Fatal(err)
	}
	if err := logger.Infof("integer: %d", 10); err != nil {
		t.Fatal(err)
	}

	expected := fmt.Sprintf("[0][%s] INFO: integer: 10\n", tag)

	t.Run("LogListenerToStdout", func(t *testing.T) {
		testToStdout(t, tag, expected)
	})

	t.Run("LogListenerToFile", func(t *testing.T) {
		testToFile(t, tag, expected)
	})

	tagToIgnore := tag
	for tagToIgnore == tag {
		tagToIgnore = genTag()
	}

	if err := logger.InfoTf(tagToIgnore, "integer: NaN"); err != nil {
		t.Fatal(err)
	}

	t.Run("LogListenerIgnoringTag", func(t *testing.T) {
		testIgnoringTag(t, tag, tagToIgnore, expected)
	})

	t.Run("LogListenerListingAndIgnoringTag", func(t *testing.T) {
		testListingAndIgnoringTag(t, tag, tagToIgnore, expected)
	})
}

func genTag() string {
	var max big.Int
	max.SetInt64(10000)
	r, err := rand.Int(rand.Reader, &max)
	if err != nil {
		log.Fatal(err)
	}
	return fmt.Sprintf("logger_test_%d", r)
}

// testToStdout runs log_listener to listen for the given tag and to write its
// output to stdout. The stdout buffer is then checked for the expected string.
func testToStdout(t *testing.T, tag, expected string) {
	cmd := exec.Command(loglistener, "--tag", tag)
	var stdout bytes.Buffer
	cmd.Stdout = &stdout
	err := cmd.Start()
	if err != nil {
		log.Fatal(err)
	}
	defer cmd.Process.Kill()

	res := tryWithBackoff(t, func() bool {
		return strings.HasSuffix(stdout.String(), expected)
	})

	if !res {
		t.Fatalf("expected suffix: %q, got: %q", expected, stdout.String())
	}
}

// testToFile runs log_listener to listen for the given tag and to write its
// output into a temporary file. The temporary file is then checked for the
// expected string.
func testToFile(t *testing.T, tag, expected string) {
	tmpfile, err := ioutil.TempFile("", "logger-test")
	if err != nil {
		t.Fatal(err)
	}
	tmpfile.Close()
	defer os.Remove(tmpfile.Name())

	cmd := exec.Command(loglistener, "--tag", tag, "--file", tmpfile.Name())
	err = cmd.Start()
	if err != nil {
		log.Fatal(err)
	}
	defer cmd.Process.Kill()

	var fileout []byte
	res := tryWithBackoff(t, func() bool {
		fileout, err = ioutil.ReadFile(tmpfile.Name())
		if err != nil {
			t.Fatal(err)
		}
		return strings.HasSuffix(string(fileout), expected)
	})

	if !res {
		t.Fatalf("expected suffix: %q, got: %q", expected, string(fileout))
	}
}

// tryWithBackoff calls f periodicaly until it returns true
func tryWithBackoff(t *testing.T, f func() bool) bool {
	step_sleep := 10    // in ms
	total_sleep := 5000 // in ms
	tries := total_sleep / step_sleep
	for i := 0; i < tries; i++ {
		if f() {
			return true
		}
		time.Sleep(time.Duration(step_sleep) * time.Millisecond)
	}

	return false
}

// testIgnoringTag runs log_listener configured to listen for `tag` and ignore
// `tagToIgnore`. The last log containing `tag` and not `tagToIgnore` is then
// compared against `expected`.
func testIgnoringTag(t *testing.T, tag, tagToIgnore, expected string) {
	cmd := exec.Command(loglistener, "--tag", tag, "--ignore-tag", tagToIgnore)
	var stdout bytes.Buffer
	cmd.Stdout = &stdout
	err := cmd.Start()
	if err != nil {
		log.Fatal(err)
	}
	defer cmd.Process.Kill()

	res := tryWithBackoff(t, func() bool {
		return strings.HasSuffix(stdout.String(), expected)
	})

	if !res {
		t.Fatalf("expected suffix: %q, got: %q", expected, stdout.String())
	}
}

// testListingAndIgnoringTag runs log_listener configured to listen for `tag`
// AND `tagToIgnore`, and to ignore `tagToIgnore`. The last log containing `tag`
// and not `tagToIgnore` is then compared against `expected` (the same behavior
// as in testIgnoringTag)
func testListingAndIgnoringTag(t *testing.T, tag, tagToIgnore, expected string) {
	cmd := exec.Command(loglistener, "--tag", tag, "--tag", tagToIgnore, "--ignore-tag", tagToIgnore)
	var stdout bytes.Buffer
	cmd.Stdout = &stdout
	err := cmd.Start()
	if err != nil {
		log.Fatal(err)
	}
	defer cmd.Process.Kill()

	res := tryWithBackoff(t, func() bool {
		return strings.HasSuffix(stdout.String(), expected)
	})

	if !res {
		t.Fatalf("expected suffix: %q, got: %q", expected, stdout.String())
	}
}
