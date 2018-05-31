// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_APP_CPP_SERVICE_PROVIDER_IMPL_H_
#define LIB_APP_CPP_SERVICE_PROVIDER_IMPL_H_

#include <lib/zx/channel.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

#include <component/cpp/fidl.h>
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"

namespace component {

// An implementation of |ServiceProvider|, which can be customized appropriately
// (to select what services it provides).
class ServiceProviderImpl : public ServiceProvider {
 public:
  // |ServiceConnector| is the generic, type-unsafe interface for objects used
  // by |ServiceProviderImpl| to connect generic "interface requests" (i.e.,
  // just channels) specified by service name to service implementations.
  using ServiceConnector = std::function<void(zx::channel)>;

  // |DefaultServiceConnector| is the default last resort service connector
  // which is called when the service provider does not recognize a particular
  // service name.  This may be used to implement service provider delegation
  // or more complex name-based service resolution strategies.
  using DefaultServiceConnector = std::function<void(std::string, zx::channel)>;

  // A |InterfaceRequestHandler<Interface>| is simply a function that
  // handles an interface request for |Interface|. If it determines that the
  // request should be "accepted", then it should "connect" ("take ownership
  // of") request. Otherwise, it can simply drop |interface_request| (as implied
  // by the interface).
  template <typename Interface>
  using InterfaceRequestHandler =
      std::function<void(fidl::InterfaceRequest<Interface> interface_request)>;

  // Constructs this service provider implementation in an unbound state.
  ServiceProviderImpl();

  // Constructs this service provider implementation, binding it to the given
  // interface request. Note: If |request| is not valid ("pending"), then the
  // object will be put into an unbound state.
  explicit ServiceProviderImpl(fidl::InterfaceRequest<ServiceProvider> request);

  ~ServiceProviderImpl() override;

  // Binds this service provider implementation to the given interface request.
  // Multiple bindings may be added.  They are automatically removed when closed
  // remotely.
  void AddBinding(fidl::InterfaceRequest<ServiceProvider> request);

  // Disconnect this service provider implementation and put it in a state where
  // it can be rebound to a new request (i.e., restores this object to an
  // unbound state). This may be called even if this object is already unbound.
  void Close();

  // Adds a supported service with the given |service_name|, using the given
  // |service_connector|.
  void AddServiceForName(ServiceConnector connector,
                         const std::string& service_name);

  // Adds a supported service with the given |service_name|, using the given
  // |interface_request_handler| (see above for information about
  // |InterfaceRequestHandler<Interface>|). |interface_request_handler| should
  // remain valid for the lifetime of this object.
  //
  // A typical usage may be:
  //
  //   service_provider_impl_->AddService<Foobar>(
  //       [](InterfaceRequest<FooBar> foobar_request) {
  //         foobar_binding_.AddBinding(this, std::move(foobar_request));
  //       });
  template <typename Interface>
  void AddService(InterfaceRequestHandler<Interface> handler,
                  const std::string& service_name = Interface::Name_) {
    AddServiceForName(
        [handler](zx::channel channel) {
          handler(fidl::InterfaceRequest<Interface>(std::move(channel)));
        },
        service_name);
  }

  // Removes support for the service with the given |service_name|.
  void RemoveServiceForName(const std::string& service_name);

  // Like |RemoveServiceForName()| (above), but designed so that it can be used
  // like |RemoveService<Interface>()| or even
  // |RemoveService<Interface>(service_name)| (to parallel
  // |AddService<Interface>()|).
  template <typename Interface>
  void RemoveService(const std::string& service_name = Interface::Name_) {
    RemoveServiceForName(service_name);
  }

  // Sets the default service connector which is called when the service
  // provider does not recognize a particular service name.  This may be used to
  // implement service provider delegation or more complex name-based service
  // resolution strategies.
  //
  // Calling this function replaces any value set previously by
  // |SetDefaultServiceConnector| or |SetDefaultServiceProvider|.
  void SetDefaultServiceConnector(DefaultServiceConnector connector);

  // Like |SetDefaultServiceConnector| but delegates requests to another
  // service provider.
  //
  // Calling this function replaces any value set previously by
  // |SetDefaultServiceConnector| or |SetDefaultServiceProvider|.
  void SetDefaultServiceProvider(ServiceProviderPtr provider);

 private:
  // Overridden from |ServiceProvider|:
  void ConnectToService(fidl::StringPtr service_name,
                        zx::channel client_handle) override;

  fidl::BindingSet<ServiceProvider> bindings_;

  std::unordered_map<std::string, ServiceConnector> name_to_service_connector_;
  DefaultServiceConnector default_service_connector_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ServiceProviderImpl);
};

}  // namespace component

#endif  // LIB_APP_CPP_SERVICE_PROVIDER_IMPL_H_
