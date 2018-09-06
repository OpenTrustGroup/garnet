// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "generic_attribute_service.h"

#include <zircon/assert.h>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/log.h"
#include "garnet/drivers/bluetooth/lib/gatt/gatt_defs.h"
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
  ZX_DEBUG_ASSERT(local_service_manager != nullptr);
  ZX_DEBUG_ASSERT(send_indication_callback_);

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

  ClientConfigCallback ccc_callback = [this](IdType service_id, IdType chrc_id,
                                             const std::string& peer_id,
                                             bool notify, bool indicate) {
    ZX_DEBUG_ASSERT(chrc_id == 0u);

    // Discover the handle assigned to this characteristic if necessary.
    if (svc_changed_handle_ == att::kInvalidHandle) {
      LocalServiceManager::ClientCharacteristicConfig config;
      if (!local_service_manager_->GetCharacteristicConfig(service_id, chrc_id,
                                                           peer_id, &config)) {
        bt_log(TRACE, "gatt",
               "service: Peer has not configured characteristic:  %s",
               peer_id.c_str());
        return;
      }
      svc_changed_handle_ = config.handle;
    }
    if (indicate) {
      subscribed_peers_.insert(peer_id);
      bt_log(SPEW, "gatt", "service: Service Changed enabled for peer  %s",
             peer_id.c_str());
    } else {
      subscribed_peers_.erase(peer_id);
      bt_log(SPEW, "gatt", "service: Service Changed disabled for peer  %s",
             peer_id.c_str());
    }
  };

  service_id_ = local_service_manager_->RegisterService(
      std::move(service), NopReadHandler, NopWriteHandler,
      std::move(ccc_callback));
  ZX_DEBUG_ASSERT(service_id_ != kInvalidId);
  local_service_manager_->set_service_changed_callback(
      fit::bind_member(this, &GenericAttributeService::OnServiceChanged));
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

  for (const auto& peer_id : subscribed_peers_) {
    bt_log(SPEW, "gatt",
           "service: indicating peer %s of service(s) changed "
           "(start: %#.4x, end: %#.4x)",
           peer_id.c_str(), start, end);
    send_indication_callback_(peer_id, svc_changed_handle_, value);
  }
}

}  // namespace gatt
}  // namespace btlib
