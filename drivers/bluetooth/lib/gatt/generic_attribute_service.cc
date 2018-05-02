// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "generic_attribute_service.h"

#include "garnet/drivers/bluetooth/lib/gatt/gatt_defs.h"
#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "lib/fxl/strings/string_number_conversions.h"

namespace btlib {
namespace gatt {
namespace {

void NopReadHandler(IdType, IdType, uint16_t, const ReadResponder&) {
}

void NopWriteHandler(IdType, IdType, uint16_t, const common::ByteBuffer&,
                     const WriteResponder&) {
}

}  // namespace


GenericAttributeService::GenericAttributeService(
    LocalServiceManager* local_service_manager,
    SendIndicationCallback send_indication_callback)
  : local_service_manager_(local_service_manager),
    send_indication_callback_(std::move(send_indication_callback)) {
  FXL_DCHECK(local_service_manager != nullptr);
  FXL_DCHECK(send_indication_callback_);

  Register();
}

GenericAttributeService::~GenericAttributeService() {
  if (local_service_manager_ != nullptr && service_id_ != kInvalidId) {
    local_service_manager_->UnregisterService(service_id_);
  }
}

void GenericAttributeService::Register() {
  const att::AccessRequirements kDisallowed;
  const att::AccessRequirements kAllowedNoSecurity(false, false, false);
  CharacteristicPtr service_changed_chr = std::make_unique<Characteristic>(
      0,                                     // id
      types::kServiceChangedCharacteristic,  // type
      Property::kIndicate,                   // properties
      0u,                                    // extended_properties
      kDisallowed,                           // read
      kDisallowed,                           // write
      kAllowedNoSecurity);                   // update
  auto service = std::make_unique<Service>(true,
                                           types::kGenericAttributeService);
  service->AddCharacteristic(std::move(service_changed_chr));

  ClientConfigCallback ccc_callback =
    [this](IdType service_id, IdType chrc_id, const std::string& peer_id,
           bool notify, bool indicate) {
    FXL_DCHECK(chrc_id == 0u);

    // Discover the handle assigned to this characteristic if necessary.
    if (svc_changed_handle_ == att::kInvalidHandle) {
      LocalServiceManager::ClientCharacteristicConfig config;
      if (!local_service_manager_->GetCharacteristicConfig(service_id, chrc_id,
                                                           peer_id, &config)) {
        FXL_VLOG(1) << "gatt: service: Peer has not configured characteristic: "
                    << peer_id;
        return;
      }
      svc_changed_handle_ = config.handle;
    }
    if (indicate) {
      subscribed_peers_.insert(peer_id);
      FXL_VLOG(2) << "gatt: service: Service Changed enabled for peer "
                  << peer_id;
    } else {
      subscribed_peers_.erase(peer_id);
      FXL_VLOG(2) << "gatt: service: Service Changed disabled for peer "
                  << peer_id;
    }
  };

  service_id_ = local_service_manager_->RegisterService(
      std::move(service), NopReadHandler, NopWriteHandler,
      std::move(ccc_callback));
  FXL_DCHECK(service_id_ != kInvalidId);
  local_service_manager_->set_service_changed_callback(
      fbl::BindMember(this, &GenericAttributeService::OnServiceChanged));
}

void GenericAttributeService::OnServiceChanged(IdType service_id,
                                               att::Handle start,
                                               att::Handle end) {
  // Service Changed not yet configured for indication.
  if (svc_changed_handle_ == att::kInvalidHandle) {
    return;
  }

  // Don't send indications for this service's removal.
  if (service_id_ == service_id) {
    return;
  }

  common::StaticByteBuffer<2 * sizeof(uint16_t)> value;

  value[0] = static_cast<uint8_t>(start);
  value[1] = static_cast<uint8_t>(start >> 8);
  value[2] = static_cast<uint8_t>(end);
  value[3] = static_cast<uint8_t>(end >> 8);

  const auto handle_to_str =
      [](att::Handle h) { return fxl::NumberToString(h, fxl::Base::k16); };
  for (const auto& peer_id : subscribed_peers_) {
    FXL_VLOG(2) << "gatt: service: indicating peer " << peer_id
                << " of service(s) changed: 0x" << handle_to_str(start)
                << " to 0x" << handle_to_str(end);
    send_indication_callback_(peer_id, svc_changed_handle_, value);
  }
}

}  // namespace gatt
}  // namespace btlib
