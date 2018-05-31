// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "client.h"

#include "garnet/drivers/bluetooth/lib/common/slab_allocator.h"
#include "lib/fxl/logging.h"

#include "gatt_defs.h"

using btlib::common::HostError;

namespace btlib {

using att::StatusCallback;
using common::BufferView;
using common::HostError;

namespace gatt {
namespace {

common::MutableByteBufferPtr NewPDU(size_t param_size) {
  auto pdu = common::NewSlabBuffer(sizeof(att::Header) + param_size);
  if (!pdu) {
    FXL_VLOG(1) << "att: Out of memory";
  }

  return pdu;
}

template <att::UUIDType Format,
          typename EntryType = att::InformationData<Format>>
bool ProcessDescriptorDiscoveryResponse(
    att::Handle range_start,
    att::Handle range_end,
    BufferView entries,
    const Client::DescriptorCallback& desc_callback,
    att::Handle* out_last_handle) {
  FXL_DCHECK(out_last_handle);

  if (entries.size() % sizeof(EntryType)) {
    FXL_VLOG(1) << "gatt:: Malformed information data list!";
    return false;
  }

  att::Handle last_handle = range_end;
  while (entries.size()) {
    const EntryType& entry = entries.As<EntryType>();

    DescriptorData desc;
    desc.handle = le16toh(entry.handle);

    // Stop and report an error if the server erroneously responds with
    // an attribute outside the requested range.
    if (desc.handle > range_end || desc.handle < range_start) {
      FXL_VLOG(1) << fxl::StringPrintf(
          "gatt: Descriptor handle out of range (handle: 0x%04x, "
          "range: 0x%04x - 0x%04x)",
          desc.handle, range_start, range_end);
      return false;
    }

    // The handles must be strictly increasing.
    if (last_handle != range_end && desc.handle <= last_handle) {
      FXL_VLOG(1) << "gatt: descriptor handles are not strictly increasing!";
      return false;
    }

    last_handle = desc.handle;
    desc.type = common::UUID(entry.uuid);

    // Notify the handler.
    desc_callback(desc);

    entries = entries.view(sizeof(EntryType));
  }

  *out_last_handle = last_handle;
  return true;
}

}  // namespace

class Impl final : public Client {
 public:
  explicit Impl(fxl::RefPtr<att::Bearer> bearer)
      : att_(bearer), weak_ptr_factory_(this) {
    FXL_DCHECK(att_);

    auto handler = [this](auto txn_id, const att::PacketReader& pdu) {
      FXL_DCHECK(pdu.opcode() == att::kNotification ||
                 pdu.opcode() == att::kIndication);

      if (pdu.payload_size() < sizeof(att::NotificationParams)) {
        // Received a malformed notification. Disconnect the link.
        FXL_VLOG(1) << "gatt: malformed notification/indication PDU";
        att_->ShutDown();
        return;
      }

      bool is_ind = pdu.opcode() == att::kIndication;
      const auto& params = pdu.payload<att::NotificationParams>();
      att::Handle handle = le16toh(params.handle);

      // Auto-confirm indications.
      if (is_ind) {
        auto pdu = NewPDU(0u);
        if (pdu) {
          att::PacketWriter(att::kConfirmation, pdu.get());
          att_->Reply(txn_id, std::move(pdu));
        } else {
          att_->ReplyWithError(txn_id, handle,
                               att::ErrorCode::kInsufficientResources);
        }
      }

      // Run the handler
      if (notification_handler_) {
        notification_handler_(
            is_ind, handle,
            BufferView(params.value, pdu.payload_size() - sizeof(att::Handle)));
      } else {
        FXL_VLOG(2) << "gatt: notification/indication dropped without handler";
      }
    };

    not_handler_id_ = att_->RegisterHandler(att::kNotification, handler);
    ind_handler_id_ = att_->RegisterHandler(att::kIndication, handler);
  }

  ~Impl() override {
    att_->UnregisterHandler(not_handler_id_);
    att_->UnregisterHandler(ind_handler_id_);
  }

 private:
  fxl::WeakPtr<Client> AsWeakPtr() override {
    return weak_ptr_factory_.GetWeakPtr();
  }

  void ExchangeMTU(MTUCallback mtu_cb) override {
    auto pdu = NewPDU(sizeof(att::ExchangeMTURequestParams));
    if (!pdu) {
      mtu_cb(att::Status(HostError::kOutOfMemory), 0);
      return;
    }

    att::PacketWriter writer(att::kExchangeMTURequest, pdu.get());
    auto params = writer.mutable_payload<att::ExchangeMTURequestParams>();
    params->client_rx_mtu = htole16(att_->preferred_mtu());

    auto rsp_cb = BindCallback([this, mtu_cb](const att::PacketReader& rsp) {
      FXL_DCHECK(rsp.opcode() == att::kExchangeMTUResponse);

      if (rsp.payload_size() != sizeof(att::ExchangeMTUResponseParams)) {
        // Received a malformed response. Disconnect the link.
        att_->ShutDown();

        mtu_cb(att::Status(HostError::kPacketMalformed), 0);
        return;
      }

      const auto& rsp_params = rsp.payload<att::ExchangeMTUResponseParams>();
      uint16_t server_mtu = le16toh(rsp_params.server_rx_mtu);

      // If the minimum value is less than the default MTU, then go with the
      // default MTU (Vol 3, Part F, 3.4.2.2).
      uint16_t final_mtu =
          std::max(att::kLEMinMTU, std::min(server_mtu, att_->preferred_mtu()));
      att_->set_mtu(final_mtu);

      mtu_cb(att::Status(), final_mtu);
    });

    auto error_cb = BindErrorCallback(
        [this, mtu_cb](att::Status status, att::Handle handle) {
          // "If the Error Response is sent by the server with the Error Code
          // set to Request Not Supported, [...] the default MTU shall be used
          // (Vol 3, Part G, 4.3.1)"
          if (status.is_protocol_error() &&
              status.protocol_error() == att::ErrorCode::kRequestNotSupported) {
            FXL_VLOG(1)
                << "gatt: Peer does not support MTU exchange: using default";
            att_->set_mtu(att::kLEMinMTU);
            mtu_cb(status, att::kLEMinMTU);
            return;
          }

          FXL_VLOG(1) << "gatt: Exchange MTU failed: " << status.ToString();
          mtu_cb(status, 0);
        });

    att_->StartTransaction(std::move(pdu), rsp_cb, error_cb);
  }

  void DiscoverPrimaryServices(ServiceCallback svc_callback,
                               StatusCallback status_callback) override {
    DiscoverPrimaryServicesInternal(att::kHandleMin, att::kHandleMax,
                                    std::move(svc_callback),
                                    std::move(status_callback));
  }

  void DiscoverPrimaryServicesInternal(att::Handle start,
                                       att::Handle end,
                                       ServiceCallback svc_callback,
                                       StatusCallback status_callback) {
    auto pdu = NewPDU(sizeof(att::ReadByGroupTypeRequestParams16));
    if (!pdu) {
      status_callback(att::Status(HostError::kOutOfMemory));
      return;
    }

    att::PacketWriter writer(att::kReadByGroupTypeRequest, pdu.get());
    auto* params =
        writer.mutable_payload<att::ReadByGroupTypeRequestParams16>();
    params->start_handle = htole16(start);
    params->end_handle = htole16(end);
    params->type = htole16(types::kPrimaryService16);

    auto rsp_cb = BindCallback([this, svc_cb = std::move(svc_callback),
                                res_cb = status_callback](
                                   const att::PacketReader& rsp) mutable {
      FXL_DCHECK(rsp.opcode() == att::kReadByGroupTypeResponse);

      if (rsp.payload_size() < sizeof(att::ReadByGroupTypeResponseParams)) {
        // Received malformed response. Disconnect the link.
        FXL_VLOG(1) << "gatt: Received malformed Read By Group Type response";
        att_->ShutDown();
        res_cb(att::Status(HostError::kPacketMalformed));
        return;
      }

      const auto& rsp_params =
          rsp.payload<att::ReadByGroupTypeResponseParams>();
      uint8_t entry_length = rsp_params.length;

      // We expect the returned attribute value to be a 16-bit or 128-bit
      // service UUID.
      constexpr size_t kAttrDataSize16 =
          sizeof(att::AttributeGroupDataEntry) + sizeof(att::AttributeType16);
      constexpr size_t kAttrDataSize128 =
          sizeof(att::AttributeGroupDataEntry) + sizeof(att::AttributeType128);

      if (entry_length != kAttrDataSize16 && entry_length != kAttrDataSize128) {
        FXL_VLOG(1) << "gatt: Invalid attribute data length!";
        att_->ShutDown();
        res_cb(att::Status(HostError::kPacketMalformed));
        return;
      }

      BufferView attr_data_list(rsp_params.attribute_data_list,
                                rsp.payload_size() - 1);
      if (attr_data_list.size() % entry_length) {
        FXL_VLOG(1) << "gatt: Malformed attribute data list!";
        att_->ShutDown();
        res_cb(att::Status(HostError::kPacketMalformed));
        return;
      }

      att::Handle last_handle = att::kHandleMax;
      while (attr_data_list.size()) {
        const auto& entry = attr_data_list.As<att::AttributeGroupDataEntry>();

        ServiceData service;
        service.range_start = le16toh(entry.start_handle);
        service.range_end = le16toh(entry.group_end_handle);

        if (service.range_end < service.range_start) {
          FXL_VLOG(1) << "gatt: Received malformed service range values!";
          res_cb(att::Status(HostError::kPacketMalformed));
          return;
        }

        last_handle = service.range_end;

        BufferView value(entry.value, entry_length - (2 * sizeof(att::Handle)));

        // This must succeed as we have performed the appropriate checks above.
        __UNUSED bool result = common::UUID::FromBytes(value, &service.type);
        FXL_DCHECK(result);

        // Notify the handler.
        svc_cb(service);

        attr_data_list = attr_data_list.view(entry_length);
      }

      // The procedure is over if we have reached the end of the handle range.
      if (last_handle == att::kHandleMax) {
        res_cb(att::Status());
        return;
      }

      // Request the next batch.
      DiscoverPrimaryServicesInternal(last_handle + 1, att::kHandleMax,
                                      std::move(svc_cb), std::move(res_cb));
    });

    auto error_cb =
        BindErrorCallback([this, res_cb = status_callback](att::Status status,
                                                           att::Handle handle) {
          // An Error Response code of "Attribute Not Found" indicates the end
          // of the procedure (v5.0, Vol 3, Part G, 4.4.1).
          if (status.is_protocol_error() &&
              status.protocol_error() == att::ErrorCode::kAttributeNotFound) {
            res_cb(att::Status());
            return;
          }

          res_cb(status);
        });

    att_->StartTransaction(std::move(pdu), rsp_cb, error_cb);
  }

  void DiscoverCharacteristics(att::Handle range_start,
                               att::Handle range_end,
                               CharacteristicCallback chrc_callback,
                               StatusCallback status_callback) override {
    FXL_DCHECK(range_start <= range_end);
    FXL_DCHECK(chrc_callback);
    FXL_DCHECK(status_callback);

    if (range_start == range_end) {
      status_callback(att::Status());
      return;
    }

    auto pdu = NewPDU(sizeof(att::ReadByTypeRequestParams16));
    if (!pdu) {
      status_callback(att::Status(HostError::kOutOfMemory));
      return;
    }

    att::PacketWriter writer(att::kReadByTypeRequest, pdu.get());
    auto* params = writer.mutable_payload<att::ReadByTypeRequestParams16>();
    params->start_handle = htole16(range_start);
    params->end_handle = htole16(range_end);
    params->type = htole16(types::kCharacteristicDeclaration16);

    auto rsp_cb = BindCallback(
        [this, range_start, range_end, chrc_cb = std::move(chrc_callback),
         res_cb = status_callback](const att::PacketReader& rsp) mutable {
          FXL_DCHECK(rsp.opcode() == att::kReadByTypeResponse);

          if (rsp.payload_size() < sizeof(att::ReadByTypeResponseParams)) {
            FXL_VLOG(1) << "gatt: Received malformed Read By Type response";
            att_->ShutDown();
            res_cb(att::Status(HostError::kPacketMalformed));
            return;
          }

          const auto& rsp_params = rsp.payload<att::ReadByTypeResponseParams>();
          uint8_t entry_length = rsp_params.length;

          // The characteristic declaration value contains:
          // 1 octet: properties
          // 2 octets: value handle
          // 2 or 16 octets: UUID
          constexpr size_t kCharacDeclSize16 = sizeof(Properties) +
                                               sizeof(att::Handle) +
                                               sizeof(att::AttributeType16);
          constexpr size_t kCharacDeclSize128 = sizeof(Properties) +
                                                sizeof(att::Handle) +
                                                sizeof(att::AttributeType128);

          constexpr size_t kAttributeDataSize16 =
              sizeof(att::AttributeData) + kCharacDeclSize16;
          constexpr size_t kAttributeDataSize128 =
              sizeof(att::AttributeData) + kCharacDeclSize128;

          if (entry_length != kAttributeDataSize16 &&
              entry_length != kAttributeDataSize128) {
            FXL_VLOG(1) << "gatt: Invalid attribute data length!";
            att_->ShutDown();
            res_cb(att::Status(HostError::kPacketMalformed));
            return;
          }

          BufferView attr_data_list(rsp_params.attribute_data_list,
                                    rsp.payload_size() - 1);
          if (attr_data_list.size() % entry_length) {
            FXL_VLOG(1) << "gatt: Malformed attribute data list!";
            att_->ShutDown();
            res_cb(att::Status(HostError::kPacketMalformed));
            return;
          }

          att::Handle last_handle = range_end;
          while (attr_data_list.size()) {
            const auto& entry = attr_data_list.As<att::AttributeData>();
            BufferView value(entry.value, entry_length - sizeof(att::Handle));

            CharacteristicData chrc;
            chrc.handle = le16toh(entry.handle);
            chrc.properties = value[0];
            chrc.value_handle = le16toh(value.view(1, 2).As<att::Handle>());

            // Vol 3, Part G, 3.3: "The Characteristic Value declaration shall
            // exist immediately following the characteristic declaration."
            if (chrc.value_handle != chrc.handle + 1) {
              FXL_VLOG(1) << "gatt: Characteristic value doesn't follow decl";
              res_cb(att::Status(HostError::kPacketMalformed));
              return;
            }

            // Stop and report an error if the server erroneously responds with
            // an attribute outside the requested range.
            if (chrc.handle > range_end || chrc.handle < range_start) {
              FXL_VLOG(1) << fxl::StringPrintf(
                  "gatt: Characteristic handle out of range (handle: 0x%04x, "
                  "range: 0x%04x - 0x%04x)",
                  chrc.handle, range_start, range_end);
              res_cb(att::Status(HostError::kPacketMalformed));
              return;
            }

            // The handles must be strictly increasing. Check this so that a
            // server cannot fool us into sending requests forever.
            if (last_handle != range_end && chrc.handle <= last_handle) {
              FXL_VLOG(1) << "gatt: Handles are not strictly increasing!";
              res_cb(att::Status(HostError::kPacketMalformed));
              return;
            }

            last_handle = chrc.handle;

            // This must succeed as we have performed the necessary checks
            // above.
            __UNUSED bool result =
                common::UUID::FromBytes(value.view(3), &chrc.type);
            FXL_DCHECK(result);

            // Notify the handler.
            chrc_cb(chrc);

            attr_data_list = attr_data_list.view(entry_length);
          }

          // The procedure is over if we have reached the end of the handle
          // range.
          if (last_handle == range_end) {
            res_cb(att::Status());
            return;
          }

          // Request the next batch.
          DiscoverCharacteristics(last_handle + 1, range_end,
                                  std::move(chrc_cb), std::move(res_cb));
        });

    auto error_cb =
        BindErrorCallback([this, res_cb = status_callback](att::Status status,
                                                           att::Handle handle) {
          // An Error Response code of "Attribute Not Found" indicates the end
          // of the procedure (v5.0, Vol 3, Part G, 4.6.1).
          if (status.is_protocol_error() &&
              status.protocol_error() == att::ErrorCode::kAttributeNotFound) {
            res_cb(att::Status());
            return;
          }

          res_cb(status);
        });

    att_->StartTransaction(std::move(pdu), rsp_cb, error_cb);
  }

  void DiscoverDescriptors(att::Handle range_start,
                           att::Handle range_end,
                           DescriptorCallback desc_callback,
                           StatusCallback status_callback) override {
    FXL_DCHECK(range_start <= range_end);
    FXL_DCHECK(desc_callback);
    FXL_DCHECK(status_callback);

    auto pdu = NewPDU(sizeof(att::FindInformationRequestParams));
    if (!pdu) {
      status_callback(att::Status(HostError::kOutOfMemory));
      return;
    }

    att::PacketWriter writer(att::kFindInformationRequest, pdu.get());
    auto* params = writer.mutable_payload<att::FindInformationRequestParams>();
    params->start_handle = htole16(range_start);
    params->end_handle = htole16(range_end);

    auto rsp_cb = BindCallback([this, range_start, range_end,
                                desc_cb = std::move(desc_callback),
                                res_cb = status_callback](
                                   const att::PacketReader& rsp) mutable {
      FXL_DCHECK(rsp.opcode() == att::kFindInformationResponse);

      if (rsp.payload_size() < sizeof(att::FindInformationResponseParams)) {
        FXL_VLOG(1) << "gatt: received malformed \"Find Information\" response";
        att_->ShutDown();
        res_cb(att::Status(HostError::kPacketMalformed));
        return;
      }

      const auto& rsp_params =
          rsp.payload<att::FindInformationResponseParams>();
      BufferView entries = rsp.payload_data().view(sizeof(rsp_params.format));

      att::Handle last_handle;
      bool result;
      switch (rsp_params.format) {
        case att::UUIDType::k16Bit:
          result = ProcessDescriptorDiscoveryResponse<att::UUIDType::k16Bit>(
              range_start, range_end, entries, desc_cb, &last_handle);
          break;
        case att::UUIDType::k128Bit:
          result = ProcessDescriptorDiscoveryResponse<att::UUIDType::k128Bit>(
              range_start, range_end, entries, desc_cb, &last_handle);
          break;
        default:
          FXL_VLOG(1) << "gatt: invalid information data format";
          result = false;
          break;
      }

      if (!result) {
        att_->ShutDown();
        res_cb(att::Status(HostError::kPacketMalformed));
        return;
      }

      // The procedure is over if we have reached the end of the handle range.
      if (last_handle == range_end) {
        res_cb(att::Status());
        return;
      }

      // Request the next batch.
      DiscoverDescriptors(last_handle + 1, range_end, std::move(desc_cb),
                          std::move(res_cb));
    });

    auto error_cb =
        BindErrorCallback([this, res_cb = status_callback](att::Status status,
                                                           att::Handle handle) {
          // An Error Response code of "Attribute Not Found" indicates the end
          // of the procedure (v5.0, Vol 3, Part G, 4.7.1).
          if (status.is_protocol_error() &&
              status.protocol_error() == att::ErrorCode::kAttributeNotFound) {
            res_cb(att::Status());
            return;
          }

          res_cb(status);
        });

    att_->StartTransaction(std::move(pdu), rsp_cb, error_cb);
  }

  void ReadRequest(att::Handle handle, ReadCallback callback) override {
    auto pdu = NewPDU(sizeof(att::ReadRequestParams));
    if (!pdu) {
      callback(att::Status(HostError::kOutOfMemory), BufferView());
      return;
    }

    att::PacketWriter writer(att::kReadRequest, pdu.get());
    auto params = writer.mutable_payload<att::ReadRequestParams>();
    params->handle = htole16(handle);

    auto rsp_cb = BindCallback([this, callback](const att::PacketReader& rsp) {
      FXL_DCHECK(rsp.opcode() == att::kReadResponse);
      callback(att::Status(), rsp.payload_data());
    });

    auto error_cb = BindErrorCallback(
        [this, callback](att::Status status, att::Handle handle) {
          FXL_VLOG(1) << "gatt: Read request failed: " << status.ToString()
                      << ", handle: " << handle;
          callback(status, BufferView());
        });

    if (!att_->StartTransaction(std::move(pdu), rsp_cb, error_cb)) {
      callback(att::Status(HostError::kPacketMalformed), BufferView());
    }
  }

  void WriteRequest(att::Handle handle,
                    const common::ByteBuffer& value,
                    StatusCallback callback) override {
    const size_t payload_size = sizeof(att::WriteRequestParams) + value.size();
    if (sizeof(att::OpCode) + payload_size > att_->mtu()) {
      FXL_VLOG(2) << "gatt: write request payload exceeds MTU";
      callback(att::Status(HostError::kPacketMalformed));
      return;
    }

    auto pdu = NewPDU(payload_size);
    if (!pdu) {
      callback(att::Status(HostError::kOutOfMemory));
      return;
    }

    att::PacketWriter writer(att::kWriteRequest, pdu.get());
    auto params = writer.mutable_payload<att::WriteRequestParams>();
    params->handle = htole16(handle);

    auto value_view =
        writer.mutable_payload_data().mutable_view(sizeof(att::Handle));
    value.Copy(&value_view);

    auto rsp_cb = BindCallback([this, callback](const att::PacketReader& rsp) {
      FXL_DCHECK(rsp.opcode() == att::kWriteResponse);

      if (rsp.payload_size()) {
        att_->ShutDown();
        callback(att::Status(HostError::kPacketMalformed));
        return;
      }

      callback(att::Status());
    });

    auto error_cb = BindErrorCallback(
        [this, callback](att::Status status, att::Handle handle) {
          FXL_VLOG(1) << "gatt: Write request failed: " << status.ToString()
                      << ", handle: " << handle;
          callback(status);
        });

    if (!att_->StartTransaction(std::move(pdu), rsp_cb, error_cb)) {
      callback(att::Status(HostError::kPacketMalformed));
    }
  }

  void SetNotificationHandler(NotificationCallback handler) override {
    notification_handler_ = std::move(handler);
  }

  // Wraps |callback| in a TransactionCallback that only runs if this Client is
  // still alive.
  att::Bearer::TransactionCallback BindCallback(
      att::Bearer::TransactionCallback callback) {
    return [self = weak_ptr_factory_.GetWeakPtr(), callback](const auto& rsp) {
      if (self) {
        callback(rsp);
      }
    };
  }

  // Wraps |callback| in a ErrorCallback that only runs if this Client is still
  // alive.
  att::Bearer::ErrorCallback BindErrorCallback(
      att::Bearer::ErrorCallback callback) {
    return [self = weak_ptr_factory_.GetWeakPtr(), callback](
               att::Status status, att::Handle handle) {
      if (self) {
        callback(status, handle);
      }
    };
  }

  fxl::RefPtr<att::Bearer> att_;
  att::Bearer::HandlerId not_handler_id_;
  att::Bearer::HandlerId ind_handler_id_;
  NotificationCallback notification_handler_;
  fxl::WeakPtrFactory<Client> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Impl);
};

// static
std::unique_ptr<Client> Client::Create(fxl::RefPtr<att::Bearer> bearer) {
  return std::make_unique<Impl>(std::move(bearer));
}

}  // namespace gatt
}  // namespace btlib
