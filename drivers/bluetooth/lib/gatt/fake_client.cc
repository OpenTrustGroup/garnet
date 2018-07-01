// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_client.h"

#include "garnet/drivers/bluetooth/lib/gatt/client.h"

namespace btlib {
namespace gatt {
namespace testing {

using att::StatusCallback;

FakeClient::FakeClient(async_t* dispatcher)
    : dispatcher_(dispatcher), weak_ptr_factory_(this) {
  FXL_DCHECK(dispatcher_);
}

fxl::WeakPtr<Client> FakeClient::AsWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void FakeClient::ExchangeMTU(MTUCallback callback) {
  auto task = [status = exchange_mtu_status_, mtu = server_mtu_,
               callback = std::move(callback)] { callback(status, mtu); };
  async::PostTask(dispatcher_, std::move(task));
}

void FakeClient::DiscoverPrimaryServices(ServiceCallback svc_callback,
                                         StatusCallback status_callback) {
  async::PostTask(dispatcher_, [this, svc_callback = std::move(svc_callback),
                                status_callback = std::move(status_callback)] {
    for (const auto& svc : services_) {
      svc_callback(svc);
    }
    status_callback(service_discovery_status_);
  });
}

void FakeClient::DiscoverCharacteristics(att::Handle range_start,
                                         att::Handle range_end,
                                         CharacteristicCallback chrc_callback,
                                         StatusCallback status_callback) {
  last_chrc_discovery_start_handle_ = range_start;
  last_chrc_discovery_end_handle_ = range_end;
  chrc_discovery_count_++;

  async::PostTask(dispatcher_, [this, range_start, range_end,
                                chrc_callback = std::move(chrc_callback),
                                status_callback = std::move(status_callback)] {
    for (const auto& chrc : chrcs_) {
      if (chrc.handle >= range_start && chrc.handle <= range_end) {
        chrc_callback(chrc);
      }
    }
    status_callback(chrc_discovery_status_);
  });
}

void FakeClient::DiscoverDescriptors(att::Handle range_start,
                                     att::Handle range_end,
                                     DescriptorCallback desc_callback,
                                     StatusCallback status_callback) {
  last_desc_discovery_start_handle_ = range_start;
  last_desc_discovery_end_handle_ = range_end;
  desc_discovery_count_++;

  att::Status status;
  if (!desc_discovery_status_target_ ||
      desc_discovery_count_ == desc_discovery_status_target_) {
    status = desc_discovery_status_;
  }

  async::PostTask(dispatcher_, [this, status, range_start, range_end,
                                desc_callback = std::move(desc_callback),
                                status_callback = std::move(status_callback)] {
    for (const auto& desc : descs_) {
      if (desc.handle >= range_start && desc.handle <= range_end) {
        desc_callback(desc);
      }
    }
    status_callback(status);
  });
}

void FakeClient::ReadRequest(att::Handle handle, ReadCallback callback) {
  if (read_request_callback_) {
    read_request_callback_(handle, std::move(callback));
  }
}

void FakeClient::WriteRequest(att::Handle handle,
                              const common::ByteBuffer& value,
                              StatusCallback callback) {
  if (write_request_callback_) {
    write_request_callback_(handle, value, std::move(callback));
  }
}

void FakeClient::SendNotification(bool indicate, att::Handle handle,
                                  const common::ByteBuffer& value) {
  if (notification_callback_) {
    notification_callback_(indicate, handle, value);
  }
}

void FakeClient::SetNotificationHandler(NotificationCallback callback) {
  notification_callback_ = std::move(callback);
}

}  // namespace testing
}  // namespace gatt
}  // namespace btlib
