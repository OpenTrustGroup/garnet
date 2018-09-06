// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gatt_remote_service_device.h"

#include <memory>

#include <ddk/binding.h>
#include <zircon/status.h>

#include "garnet/drivers/bluetooth/lib/common/log.h"

using namespace btlib;

namespace bthost {

namespace {

void CopyUUIDBytes(bt_gatt_uuid_t* dest, const common::UUID source) {
  memcpy(dest->bytes, source.value().data(), sizeof(dest->bytes));
}

bt_gatt_err_t ATTErrorToDDKError(btlib::att::ErrorCode error) {
  // Both of these enums *should* be identical and values.
  // Being explict so we get compiler warnings if either changes.
  switch (error) {
    case btlib::att::ErrorCode::kNoError:
      return BT_GATT_ERR_NO_ERROR;
    case btlib::att::ErrorCode::kInvalidHandle:
      return BT_GATT_ERR_INVALID_HANDLE;
    case btlib::att::ErrorCode::kReadNotPermitted:
      return BT_GATT_ERR_READ_NOT_PERMITTED;
    case btlib::att::ErrorCode::kWriteNotPermitted:
      return BT_GATT_ERR_WRITE_NOT_PERMITTED;
    case btlib::att::ErrorCode::kInvalidPDU:
      return BT_GATT_ERR_INVALID_PDU;
    case btlib::att::ErrorCode::kInsufficientAuthentication:
      return BT_GATT_ERR_INSUFFICIENT_AUTHENTICATION;
    case btlib::att::ErrorCode::kRequestNotSupported:
      return BT_GATT_ERR_REQUEST_NOT_SUPPORTED;
    case btlib::att::ErrorCode::kInvalidOffset:
      return BT_GATT_ERR_INVALID_OFFSET;
    case btlib::att::ErrorCode::kInsufficientAuthorization:
      return BT_GATT_ERR_INSUFFICIENT_AUTHORIZATION;
    case btlib::att::ErrorCode::kPrepareQueueFull:
      return BT_GATT_ERR_PREPARE_QUEUE_FULL;
    case btlib::att::ErrorCode::kAttributeNotFound:
      return BT_GATT_ERR_ATTRIBUTE_NOT_FOUND;
    case btlib::att::ErrorCode::kAttributeNotLong:
      return BT_GATT_ERR_INVALID_ATTRIBUTE_VALUE_LENGTH;
    case btlib::att::ErrorCode::kInsufficientEncryptionKeySize:
      return BT_GATT_ERR_INSUFFICIENT_ENCRYPTION_KEY_SIZE;
    case btlib::att::ErrorCode::kInvalidAttributeValueLength:
      return BT_GATT_ERR_INVALID_ATTRIBUTE_VALUE_LENGTH;
    case btlib::att::ErrorCode::kUnlikelyError:
      return BT_GATT_ERR_UNLIKELY_ERROR;
    case btlib::att::ErrorCode::kInsufficientEncryption:
      return BT_GATT_ERR_INSUFFICIENT_ENCRYPTION;
    case btlib::att::ErrorCode::kUnsupportedGroupType:
      return BT_GATT_ERR_UNSUPPORTED_GROUP_TYPE;
    case btlib::att::ErrorCode::kInsufficientResources:
      return BT_GATT_ERR_INSUFFICIENT_RESOURCES;
  }
  return BT_GATT_ERR_NO_ERROR;
}

}  // namespace

GattRemoteServiceDevice::GattRemoteServiceDevice(
    zx_device_t* parent_device, const std::string& peer_id,
    fbl::RefPtr<btlib::gatt::RemoteService> service)
    : loop_(&kAsyncLoopConfigNoAttachToThread),
      parent_device_(parent_device),
      dev_(nullptr),
      peer_id_(peer_id),
      service_(service) {
  dev_proto_.version = DEVICE_OPS_VERSION;
  dev_proto_.unbind = &GattRemoteServiceDevice::DdkUnbind;
  dev_proto_.release = &GattRemoteServiceDevice::DdkRelease;
}

GattRemoteServiceDevice::~GattRemoteServiceDevice() {
  if (dev_ != nullptr) {
    device_remove(dev_);
    dev_ = nullptr;
  }
}

bt_gatt_svc_ops_t GattRemoteServiceDevice::proto_ops_ = {
    .connect = &GattRemoteServiceDevice::OpConnect,
    .stop = &GattRemoteServiceDevice::OpStop,
    .read_characteristic = &GattRemoteServiceDevice::OpReadCharacteristic,
    .read_long_characteristic =
        &GattRemoteServiceDevice::OpReadLongCharacteristic,
    .write_characteristic = &GattRemoteServiceDevice::OpWriteCharacteristic,
    .enable_notifications = &GattRemoteServiceDevice::OpEnableNotifications,
};

zx_status_t GattRemoteServiceDevice::Bind() {
  // The bind program of an attaching device driver can eiher bind using to the
  // well known short 16 bit UUID of the service if available or the full 128
  // bit UUID (split across 4 32 bit values).
  const common::UUID& uuid = service_->uuid();
  uint32_t uuid16 = 0;

  if (uuid.CompactSize() == 2) {
    uuid16 =
        le16toh(*reinterpret_cast<const uint16_t*>(uuid.CompactView().data()));
  }

  uint32_t uuid01, uuid02, uuid03, uuid04 = 0;
  common::UInt128 uuid_bytes = uuid.value();

  uuid01 = le32toh(*reinterpret_cast<uint32_t*>(&uuid_bytes[0]));
  uuid02 = le32toh(*reinterpret_cast<uint32_t*>(&uuid_bytes[4]));
  uuid03 = le32toh(*reinterpret_cast<uint32_t*>(&uuid_bytes[8]));
  uuid04 = le32toh(*reinterpret_cast<uint32_t*>(&uuid_bytes[12]));

  zx_device_prop_t props[] = {
      {BIND_BT_GATT_SVC_UUID16, 0, uuid16},
      {BIND_BT_GATT_SVC_UUID128_1, 0, uuid01},
      {BIND_BT_GATT_SVC_UUID128_2, 0, uuid02},
      {BIND_BT_GATT_SVC_UUID128_3, 0, uuid03},
      {BIND_BT_GATT_SVC_UUID128_4, 0, uuid04},
  };

  bt_log(TRACE, "bt-host",
         "bt-gatt-svc binding to UUID16(%#04x), UUID128(1: %08x, 2: %08x,"
         " 3: %08x, 4: %08x), peer: %s",
         uuid16, uuid01, uuid02, uuid03, uuid04, peer_id_.c_str());

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "bt-gatt-svc",
      .ctx = this,
      .ops = &dev_proto_,
      .proto_id = ZX_PROTOCOL_BT_GATT_SVC,
      .proto_ops = &proto_ops_,
      .props = props,
      .prop_count = 5,
      .flags = 0,
  };

  zx_status_t status = device_add(parent_device_, &args, &dev_);

  if (status != ZX_OK) {
    dev_ = nullptr;
    bt_log(ERROR, "bt-host",
           "bt-gatt-svc: failed to publish child gatt device: %s",
           zx_status_get_string(status));
    return status;
  }

  loop_.StartThread("bt-host bt-gatt-svc");

  return status;
}

void GattRemoteServiceDevice::Unbind() {
  bt_log(TRACE, "bt-host", "bt-gatt-svc: unbinding service");
  async::PostTask(loop_.dispatcher(), [this]() { loop_.Shutdown(); });
  loop_.JoinThreads();
}
void GattRemoteServiceDevice::Release() { dev_ = nullptr; }

zx_status_t GattRemoteServiceDevice::Connect(void* cookie,
                                             bt_gatt_connect_cb connect_cb) {
  async::PostTask(loop_.dispatcher(), [this, connect_cb, cookie]() {
    service_->DiscoverCharacteristics(
        [connect_cb, cookie](att::Status cb_status, const auto& chrcs) {
          auto ddk_chars = std::make_unique<bt_gatt_chr[]>(chrcs.size());
          size_t char_idx = 0;
          for (auto& chr : chrcs) {
            ddk_chars[char_idx].id = static_cast<bt_gatt_id_t>(chr.id());
            CopyUUIDBytes(&ddk_chars[char_idx].type, chr.info().type);
            ddk_chars[char_idx].properties = chr.info().properties;

            // TODO(zbowling): remote extended properties are not implemented.
            // ddk_chars[char_idx].extended_properties =
            // chr.info().extended_properties;

            auto& descriptors = chr.descriptors();
            if (descriptors.size() > 0) {
              ddk_chars[char_idx].descriptors =
                  new bt_gatt_descriptor_t[descriptors.size()];
              ddk_chars[char_idx].num_descriptors = descriptors.size();
              size_t desc_idx = 0;
              for (auto& descriptor : descriptors) {
                ddk_chars[char_idx].descriptors[desc_idx].id =
                    static_cast<bt_gatt_id_t>(descriptor.id());
                CopyUUIDBytes(&ddk_chars[char_idx].descriptors[desc_idx].type,
                              descriptor.info().type);
                desc_idx++;
              }
            } else {
              ddk_chars[char_idx].num_descriptors = 0;
              ddk_chars[char_idx].descriptors = nullptr;
            }

            char_idx++;
          }

          bt_log(TRACE, "bt-host",
                 "bt-gatt-svc: connected; discovered %zu characteristics",
                 char_idx);
          bt_gatt_status_t status = {.status = ZX_OK};
          connect_cb(cookie, status, ddk_chars.get(), char_idx);

          // Cleanup.
          for (char_idx = 0; char_idx < chrcs.size(); char_idx++) {
            if (ddk_chars[char_idx].descriptors != nullptr) {
              delete[] ddk_chars[char_idx].descriptors;
              ddk_chars[char_idx].descriptors = nullptr;
            }
          }
        },
        loop_.dispatcher());
  });

  return ZX_OK;
}

void GattRemoteServiceDevice::Stop() {
  // TODO(zbowling): Unregister notifications on the remote service.
  // We may replace this with an explict unregister for notifications instead.
}

zx_status_t GattRemoteServiceDevice::ReadCharacteristic(
    bt_gatt_id_t id, void* cookie, bt_gatt_read_characteristic_cb read_cb) {
  auto read_callback = [id, cookie, read_cb](att::Status status,
                                             const common::ByteBuffer& buff) {
    bt_gatt_err_t error_code = ATTErrorToDDKError(status.protocol_error());
    bt_gatt_status_t ddk_status = {
        .status = ZX_OK,
        .att_ecode = error_code,
    };
    read_cb(cookie, ddk_status, id, buff.data(), buff.size());
  };
  service_->ReadCharacteristic(static_cast<btlib::gatt::IdType>(id),
                               std::move(read_callback), loop_.dispatcher());

  return ZX_OK;
}

zx_status_t GattRemoteServiceDevice::ReadLongCharacteristic(
    bt_gatt_id_t id, void* cookie, uint16_t offset, size_t max_bytes,
    bt_gatt_read_characteristic_cb read_cb) {
  auto read_callback = [id, cookie, read_cb](att::Status status,
                                             const common::ByteBuffer& buff) {
    bt_gatt_err_t error_code = ATTErrorToDDKError(status.protocol_error());
    bt_gatt_status_t ddk_status = {
        .status = ZX_OK,
        .att_ecode = error_code,
    };
    read_cb(cookie, ddk_status, id, buff.data(), buff.size());
  };
  service_->ReadLongCharacteristic(static_cast<btlib::gatt::IdType>(id), offset,
                                   max_bytes, std::move(read_callback),
                                   loop_.dispatcher());

  return ZX_OK;
}

zx_status_t GattRemoteServiceDevice::WriteCharacteristic(
    bt_gatt_id_t id, void* cookie, const uint8_t* buff, size_t len,
    bt_gatt_status_cb write_cb) {
  std::vector<uint8_t> data(buff, buff + len);
  if (write_cb == nullptr) {
    service_->WriteCharacteristicWithoutResponse(
        static_cast<btlib::gatt::IdType>(id), std::move(data));
  } else {
    auto status_callback = [cookie, id, write_cb](btlib::att::Status status) {
      bt_gatt_err_t error_code = ATTErrorToDDKError(status.protocol_error());
      bt_gatt_status_t ddk_status = {
          .status = ZX_OK,
          .att_ecode = error_code,
      };
      write_cb(cookie, ddk_status, id);
    };

    service_->WriteCharacteristic(static_cast<btlib::gatt::IdType>(id),
                                  std::move(data), std::move(status_callback),
                                  loop_.dispatcher());
  }
  return ZX_OK;
}

zx_status_t GattRemoteServiceDevice::EnableNotifications(
    bt_gatt_id_t id, void* cookie, bt_gatt_status_cb status_cb,
    bt_gatt_notification_value_cb value_cb) {
  auto notif_callback = [cookie, id, value_cb](const common::ByteBuffer& buff) {
    value_cb(cookie, id, buff.data(), buff.size());
  };

  auto status_callback = [cookie, id, status_cb](
                             btlib::att::Status status,
                             btlib::gatt::IdType handler_id) {
    bt_gatt_err_t error_code = ATTErrorToDDKError(status.protocol_error());
    bt_gatt_status_t ddk_status = {
        .status = ZX_OK,
        .att_ecode = error_code,
    };
    status_cb(cookie, ddk_status, id);
  };

  service_->EnableNotifications(static_cast<btlib::gatt::IdType>(id),
                                notif_callback, std::move(status_callback),
                                loop_.dispatcher());

  return ZX_OK;
}

}  // namespace bthost
