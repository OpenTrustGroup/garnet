// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/string.h>
#include <ree_agent/cpp/fidl.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/logging.h"
#include "lib/ree_agent/cpp/object.h"
#include "lib/svc/cpp/services.h"

namespace ree_agent {

class TipcChannelImpl;

class TipcPortImpl : public TipcPort, public TipcObject {
 public:
  TipcPortImpl(uint32_t num_items, size_t item_size)
      : binding_(this), num_items_(num_items), item_size_(item_size) {}
  TipcPortImpl() = delete;

  zx_status_t Accept(fbl::RefPtr<TipcChannelImpl>* channel_out);

  ObjectType get_type() override { return ObjectType::PORT; }

  void Bind(fidl::InterfaceRequest<TipcPort> request) {
    binding_.Bind(std::move(request));
  }

 protected:
  void Connect(fidl::InterfaceHandle<TipcChannel> peer_handle,
               ConnectCallback callback) override;
  void GetInfo(GetInfoCallback callback) override;

 private:
  fidl::Binding<TipcPort> binding_;
  fidl::StringPtr path_;

  uint32_t num_items_;
  size_t item_size_;

  fbl::Mutex mutex_;
  fbl::DoublyLinkedList<fbl::RefPtr<TipcChannelImpl>> pending_requests_
      FXL_GUARDED_BY(mutex_);

  void AddPendingRequest(fbl::RefPtr<TipcChannelImpl> channel);
  fbl::RefPtr<TipcChannelImpl> GetPendingRequest();
  bool HasPendingRequests();
};

}  // namespace ree_agent
