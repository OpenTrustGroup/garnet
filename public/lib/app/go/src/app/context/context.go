// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package context

import (
	"fidl/bindings"
	"fmt"
	"svc/svcns"

	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/mxruntime"

	"fidl/component"
)

type Connector struct {
	serviceRoot zx.Handle
}

type Context struct {
	connector *Connector

	Environment     *component.EnvironmentInterface
	OutgoingService *svcns.Namespace
	Launcher        *component.ApplicationLauncherInterface
	appServices     zx.Handle
	services        bindings.BindingSet
}

// TODO: define these in syscall/zx/mxruntime
const (
	HandleDirectoryRequest mxruntime.HandleType = 0x3B
	HandleAppServices      mxruntime.HandleType = 0x43
)

func getServiceRoot() zx.Handle {
	c0, c1, err := zx.NewChannel(0)
	if err != nil {
		return zx.HandleInvalid
	}

	// TODO: Use "/svc" once that actually works.
	err = fdio.ServiceConnect("/svc/.", zx.Handle(c0))
	if err != nil {
		return zx.HandleInvalid
	}
	return zx.Handle(c1)
}

func New(serviceRoot, directoryRequest, appServices zx.Handle) *Context {
	c := &Context{
		connector: &Connector{
			serviceRoot: serviceRoot,
		},
		appServices: appServices,
	}

	c.OutgoingService = svcns.New()

	r, p, err := component.NewEnvironmentInterfaceRequest()
	if err != nil {
		panic(err.Error())
	}
	c.Environment = p
	c.ConnectToEnvService(r)

	r2, p2, err := component.NewApplicationLauncherInterfaceRequest()
	if err != nil {
		panic(err.Error())
	}
	c.Launcher = p2
	c.ConnectToEnvService(r2)

	if directoryRequest.IsValid() {
		c.OutgoingService.ServeDirectory(directoryRequest)
	}

	return c
}

func (c *Context) GetConnector() *Connector {
	return c.connector
}

func (c *Context) Serve() {
	if c.appServices.IsValid() {
		stub := component.ServiceProviderStub{Impl: c.OutgoingService}
		c.services.Add(&stub, zx.Channel(c.appServices), nil)
		go bindings.Serve()
	}
	if c.OutgoingService.Dispatcher != nil {
		go c.OutgoingService.Dispatcher.Serve()
	}
}

func (c *Context) ConnectToEnvService(r bindings.ServiceRequest) {
	c.connector.ConnectToEnvService(r)
}

func (c *Connector) ConnectToEnvService(r bindings.ServiceRequest) {
	c.ConnectToEnvServiceAt(r.Name(), r.ToChannel())
}

func (c *Connector) ConnectToEnvServiceAt(name string, h zx.Channel) {
	err := fdio.ServiceConnectAt(c.serviceRoot, name, zx.Handle(h))
	if err != nil {
		panic(fmt.Sprintf("ConnectToEnvService: %v: %v", name, err))
	}
}

func CreateFromStartupInfo() *Context {
	directoryRequest := mxruntime.GetStartupHandle(
		mxruntime.HandleInfo{Type: HandleDirectoryRequest, Arg: 0})
	appServices := mxruntime.GetStartupHandle(
		mxruntime.HandleInfo{Type: HandleAppServices, Arg: 0})
	return New(getServiceRoot(), directoryRequest, appServices)
}
