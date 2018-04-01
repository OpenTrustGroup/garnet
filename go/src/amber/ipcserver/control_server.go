// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ipcserver

import (
	"fmt"
	"strings"

	"fidl/bindings2"

	"fuchsia/go/amber"

	"amber/daemon"
	"amber/pkg"

	"syscall/zx"
)

type ControlSrvr struct {
	daemon *daemon.Daemon
	bs     bindings2.BindingSet
}

func NewControlSrvr(d *daemon.Daemon) *ControlSrvr {
	go bindings2.Serve()
	return &ControlSrvr{daemon: d}
}

func (c *ControlSrvr) DoTest(in int32) (out string, err error) {
	r := fmt.Sprintf("Your number was %d\n", in)
	return r, nil
}

func (c *ControlSrvr) AddSrc(url string, rateLimit int32, pubKey string) (bool, error) {
	return true, nil
}

func (c *ControlSrvr) RemoveSrc(url string) (bool, error) {
	return true, nil
}

func (c *ControlSrvr) Check() (bool, error) {
	return true, nil
}

func (c *ControlSrvr) ListSrcs() ([]string, error) {
	return []string{}, nil
}

func (c *ControlSrvr) GetUpdate(name string, version *string) (*string, error) {
	d := ""
	if version == nil {
		version = &d
	}
	if len(name) == 0 {
		return nil, fmt.Errorf("No name provided")
	}

	if name[0] != '/' {
		name = fmt.Sprintf("/%s", name)
	}

	ps := pkg.NewPackageSet()
	pkg := pkg.Package{Name: name, Version: *version}
	ps.Add(&pkg)

	updates := c.daemon.GetUpdates(ps)
	res, ok := updates[pkg]
	if !ok {
		return nil, fmt.Errorf("No update available")
	}
	if res.Err != nil {
		return nil, res.Err
	}

	_, err := daemon.WriteUpdateToPkgFS(res)
	if err != nil {
		return nil, err
	}

	return &res.Update.Merkle, nil
}

func (c *ControlSrvr) GetBlob(merkle string) error {
	if len(strings.TrimSpace(merkle)) == 0 {
		return fmt.Errorf("Supplied merkle root is empty")
	}

	return c.daemon.GetBlob(merkle)
}

func (c *ControlSrvr) Quit() {
	c.bs.Close()
}

func (c *ControlSrvr) Bind(ch zx.Channel) error {
	s := amber.ControlStub{Impl: c}
	return c.bs.Add(&s, ch)
}
