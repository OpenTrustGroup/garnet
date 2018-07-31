// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/string.h>
#include <ree_agent/cpp/fidl.h>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/logging.h"
#include "lib/gzos/trusty_ipc/cpp/object.h"

namespace trusty_ipc {

enum {
  /* allow Trusted Apps to connect to this port */
  IPC_PORT_ALLOW_TA_CONNECT = 0x1,
  /* allow non-secure clients to connect to this port */
  IPC_PORT_ALLOW_NS_CONNECT = 0x2,
};

class TipcChannelImpl;

class TipcPortImpl : public TipcPort, public TipcObject {
 public:
  TipcPortImpl(uint32_t num_items, size_t item_size, uint32_t flags)
      : num_items_(num_items), item_size_(item_size), flags_(flags) {}
  TipcPortImpl() = delete;

  zx_status_t Accept(std::string* uuid_out,
                     fbl::RefPtr<TipcChannelImpl>* channel_out);

  void Bind(fidl::InterfaceRequest<TipcPort> request) {
    bindings_.AddBinding(this, std::move(request));
  }

  void Close() override;

  uint32_t flags() { return flags_; }
  std::string& name() { return name_; }
  void set_name(const char* c_str) { name_ = c_str; }

 protected:
  ObjectType get_type() override { return ObjectType::PORT; }

  void Connect(fidl::InterfaceHandle<TipcChannel> peer_handle,
               fidl::StringPtr uuid, ConnectCallback callback) override;
  void GetInfo(GetInfoCallback callback) override;

 private:
  fidl::BindingSet<TipcPort> bindings_;
  std::string name_;

  uint32_t num_items_;
  size_t item_size_;
  uint32_t flags_;

  fbl::Mutex mutex_;
  fbl::DoublyLinkedList<fbl::RefPtr<TipcChannelImpl>> pending_requests_
      FXL_GUARDED_BY(mutex_);

  void AddPendingRequest(fbl::RefPtr<TipcChannelImpl> channel);
  void RemoveFromPendingRequest(fbl::RefPtr<TipcChannelImpl> channel);
  fbl::RefPtr<TipcChannelImpl> GetPendingRequest();
  bool HasPendingRequests();
};

class PortConnectFacade {
 public:
  using PortServiceConnector =
      std::function<zx_status_t(TipcPortSyncPtr& port, std::string path)>;

  PortConnectFacade(fbl::RefPtr<TipcChannelImpl> channel,
                    PortServiceConnector connector)
      : channel_(std::move(channel)),
        port_service_connector_(std::move(connector)) {}

  zx_status_t Connect(std::string path, fidl::StringPtr uuid);

 private:
  fbl::RefPtr<TipcChannelImpl> channel_;
  PortServiceConnector port_service_connector_;
};

}  // namespace trusty_ipc
