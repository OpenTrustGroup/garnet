// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gatt_server_server.h"

#include "garnet/drivers/bluetooth/lib/common/log.h"
#include "garnet/drivers/bluetooth/lib/common/uuid.h"
#include "garnet/drivers/bluetooth/lib/gap/low_energy_connection_manager.h"
#include "garnet/drivers/bluetooth/lib/gatt/connection.h"
#include "garnet/drivers/bluetooth/lib/gatt/gatt_defs.h"
#include "garnet/drivers/bluetooth/lib/gatt/server.h"

#include "helpers.h"

#include "lib/fxl/functional/make_copyable.h"

using fuchsia::bluetooth::ErrorCode;
using fuchsia::bluetooth::Status;
using GattErrorCode = fuchsia::bluetooth::gatt::ErrorCode;

using fuchsia::bluetooth::gatt::Characteristic;
using fuchsia::bluetooth::gatt::Descriptor;
using fuchsia::bluetooth::gatt::LocalService;
using fuchsia::bluetooth::gatt::LocalServiceDelegate;
using fuchsia::bluetooth::gatt::LocalServiceDelegatePtr;
using fuchsia::bluetooth::gatt::SecurityRequirementsPtr;
using fuchsia::bluetooth::gatt::ServiceInfo;

namespace bthost {
namespace {

::btlib::att::ErrorCode GattErrorCodeFromFidl(GattErrorCode error_code,
                                              bool is_read) {
  switch (error_code) {
    case GattErrorCode::NO_ERROR:
      return ::btlib::att::ErrorCode::kNoError;
    case GattErrorCode::INVALID_OFFSET:
      return ::btlib::att::ErrorCode::kInvalidOffset;
    case GattErrorCode::INVALID_VALUE_LENGTH:
      return ::btlib::att::ErrorCode::kInvalidAttributeValueLength;
    case GattErrorCode::NOT_PERMITTED:
      if (is_read)
        return ::btlib::att::ErrorCode::kReadNotPermitted;
      return ::btlib::att::ErrorCode::kWriteNotPermitted;
    default:
      break;
  }
  return ::btlib::att::ErrorCode::kUnlikelyError;
}

::btlib::att::AccessRequirements ParseSecurityRequirements(
    const SecurityRequirementsPtr& reqs) {
  if (!reqs) {
    return ::btlib::att::AccessRequirements();
  }
  return ::btlib::att::AccessRequirements(reqs->encryption_required,
                                          reqs->authentication_required,
                                          reqs->authorization_required);
}

// Carries a either a successful Result or an error message that can be sent as
// a FIDL response.
template <typename Result, typename Error = std::string,
          typename = std::enable_if_t<!std::is_same<Result, Error>::value>>
struct MaybeResult final {
  explicit MaybeResult(Result&& result)
      : result(std::forward<Result>(result)) {}

  explicit MaybeResult(Error&& error) : error(std::forward<Error>(error)) {}

  bool is_error() const { return static_cast<bool>(!result); }

  Result result;
  Error error;
};

using DescriptorResult = MaybeResult<::btlib::gatt::DescriptorPtr>;
DescriptorResult NewDescriptor(const Descriptor& fidl_desc) {
  auto read_reqs = ParseSecurityRequirements(fidl_desc.permissions->read);
  auto write_reqs = ParseSecurityRequirements(fidl_desc.permissions->write);

  ::btlib::common::UUID type;
  if (!::btlib::common::StringToUuid(fidl_desc.type.get(), &type)) {
    return DescriptorResult("Invalid descriptor UUID");
  }

  return DescriptorResult(std::make_unique<::btlib::gatt::Descriptor>(
      fidl_desc.id, type, read_reqs, write_reqs));
}

using CharacteristicResult = MaybeResult<::btlib::gatt::CharacteristicPtr>;
CharacteristicResult NewCharacteristic(const Characteristic& fidl_chrc) {
  uint8_t props = fidl_chrc.properties & 0xFF;
  uint16_t ext_props = (fidl_chrc.properties & 0xFF00) >> 8;

  if (!fidl_chrc.permissions) {
    return CharacteristicResult("Characteristic permissions missing");
  }

  bool supports_update = (props & ::btlib::gatt::Property::kNotify) ||
                         (props & ::btlib::gatt::Property::kIndicate);
  if (supports_update != static_cast<bool>(fidl_chrc.permissions->update)) {
    return CharacteristicResult(
        supports_update ? "Characteristic update permission required"
                        : "Characteristic update permission must be null");
  }

  auto read_reqs = ParseSecurityRequirements(fidl_chrc.permissions->read);
  auto write_reqs = ParseSecurityRequirements(fidl_chrc.permissions->write);
  auto update_reqs = ParseSecurityRequirements(fidl_chrc.permissions->update);

  ::btlib::common::UUID type;
  if (!::btlib::common::StringToUuid(fidl_chrc.type.get(), &type)) {
    return CharacteristicResult("Invalid characteristic UUID");
  }

  auto chrc = std::make_unique<::btlib::gatt::Characteristic>(
      fidl_chrc.id, type, props, ext_props, read_reqs, write_reqs, update_reqs);
  if (fidl_chrc.descriptors && !fidl_chrc.descriptors->empty()) {
    for (const auto& fidl_desc : *fidl_chrc.descriptors) {
      auto desc_result = NewDescriptor(fidl_desc);
      if (desc_result.is_error()) {
        return CharacteristicResult(std::move(desc_result.error));
      }

      chrc->AddDescriptor(std::move(desc_result.result));
    }
  }

  return CharacteristicResult(std::move(chrc));
}

}  // namespace

// Implements the gatt::LocalService FIDL interface. Instances of this class are
// only created by a GattServerServer.
class GattServerServer::LocalServiceImpl
    : public GattServerBase<fuchsia::bluetooth::gatt::LocalService> {
 public:
  LocalServiceImpl(GattServerServer* owner, uint64_t id,
                   LocalServiceDelegatePtr delegate,
                   ::fidl::InterfaceRequest<LocalService> request)
      : GattServerBase(owner->gatt(), this, std::move(request)),
        owner_(owner),
        id_(id),
        delegate_(std::move(delegate)) {
    ZX_DEBUG_ASSERT(owner_);
    ZX_DEBUG_ASSERT(delegate_);
  }

  // The destructor removes the GATT service
  ~LocalServiceImpl() override {
    CleanUp();

    // Do not notify the owner in this case. If we got here it means that
    // |owner_| deleted us.
  }

  // Returns the current delegate. Returns nullptr if the delegate was
  // disconnected (e.g. due to a call to RemoveService()).
  LocalServiceDelegate* delegate() { return delegate_.get(); }

 private:
  // fuchsia::bluetooth::gatt::Service overrides:
  void RemoveService() override {
    CleanUp();
    owner_->RemoveService(id_);
  }

  void NotifyValue(uint64_t characteristic_id, ::fidl::StringPtr peer_id,
                   ::fidl::VectorPtr<uint8_t> value, bool confirm) override {
    gatt()->SendNotification(id_, characteristic_id, peer_id, std::move(value),
                             confirm);
  }

  // Unregisters the underlying service if it is still active.
  void CleanUp() {
    delegate_ = nullptr;  // Closes the delegate handle.
    gatt()->UnregisterService(id_);
  }

  // |owner_| owns this instance and is expected to outlive it.
  GattServerServer* owner_;  // weak
  uint64_t id_;

  // The delegate connection for the corresponding service instance. This gets
  // cleared when the service is unregistered (via RemoveService() or
  // destruction).
  LocalServiceDelegatePtr delegate_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LocalServiceImpl);
};

GattServerServer::GattServerServer(
    fbl::RefPtr<btlib::gatt::GATT> gatt,
    fidl::InterfaceRequest<fuchsia::bluetooth::gatt::Server> request)
    : GattServerBase(gatt, this, std::move(request)), weak_ptr_factory_(this) {}

GattServerServer::~GattServerServer() {
  // This will remove all of our services from their adapter.
  services_.clear();
}

void GattServerServer::RemoveService(uint64_t id) {
  if (services_.erase(id)) {
    bt_log(TRACE, "bt-host", "service removed (id: %u)", id);
  } else {
    bt_log(TRACE, "bt-host", "service id not found: %u", id);
  }
}

void GattServerServer::PublishService(
    ServiceInfo service_info,
    fidl::InterfaceHandle<LocalServiceDelegate> delegate,
    fidl::InterfaceRequest<LocalService> service_iface,
    PublishServiceCallback callback) {
  if (!delegate) {
    auto error = fidl_helpers::NewFidlError(ErrorCode::INVALID_ARGUMENTS,
                                            "A delegate is required");
    callback(std::move(error));
    return;
  }

  if (!service_iface) {
    auto error = fidl_helpers::NewFidlError(ErrorCode::INVALID_ARGUMENTS,
                                            "Service interface is required");
    callback(std::move(error));
    return;
  }

  ::btlib::common::UUID service_type;
  if (!::btlib::common::StringToUuid(service_info.type.get(), &service_type)) {
    auto error = fidl_helpers::NewFidlError(ErrorCode::INVALID_ARGUMENTS,
                                            "Invalid service UUID");
    callback(std::move(error));
    return;
  }

  // Process the FIDL service tree.
  auto service = std::make_unique<::btlib::gatt::Service>(service_info.primary,
                                                          service_type);
  if (service_info.characteristics) {
    for (const auto& fidl_chrc : *service_info.characteristics) {
      auto chrc_result = NewCharacteristic(fidl_chrc);
      if (chrc_result.is_error()) {
        auto error = fidl_helpers::NewFidlError(ErrorCode::INVALID_ARGUMENTS,
                                                chrc_result.error);
        callback(std::move(error));
        return;
      }

      service->AddCharacteristic(std::move(chrc_result.result));
    }
  }

  auto self = weak_ptr_factory_.GetWeakPtr();

  // Set up event handlers.
  auto read_handler = [self](auto svc_id, auto id, auto offset,
                             auto responder) mutable {
    if (self) {
      self->OnReadRequest(svc_id, id, offset, std::move(responder));
    } else {
      responder(::btlib::att::ErrorCode::kUnlikelyError,
                ::btlib::common::BufferView());
    }
  };
  auto write_handler = [self](auto svc_id, auto id, auto offset,
                              const auto& value, auto responder) mutable {
    if (self) {
      self->OnWriteRequest(svc_id, id, offset, value, std::move(responder));
    } else {
      responder(::btlib::att::ErrorCode::kUnlikelyError);
    }
  };
  auto ccc_callback = [self](auto svc_id, auto id, const std::string& peer_id,
                             bool notify, bool indicate) {
    if (self)
      self->OnCharacteristicConfig(svc_id, id, peer_id, notify, indicate);
  };

  auto id_cb = [self, delegate = std::move(delegate),
                service_iface = std::move(service_iface),
                callback =
                    std::move(callback)](btlib::gatt::IdType id) mutable {
    if (!self)
      return;

    if (!id) {
      // TODO(armansito): Report a more detailed string if registration
      // fails due to duplicate ids.
      auto error = fidl_helpers::NewFidlError(ErrorCode::FAILED,
                                              "Failed to publish service");
      callback(std::move(error));
      return;
    }

    ZX_DEBUG_ASSERT(self->services_.find(id) == self->services_.end());

    // This will be called if either the delegate or the service connection
    // closes.
    auto connection_error_cb = [self, id] {
      bt_log(TRACE, "bt-host", "removing GATT service (id: %u)", id);
      if (self)
        self->RemoveService(id);
    };

    auto delegate_ptr = delegate.Bind();
    delegate_ptr.set_error_handler(connection_error_cb);

    auto service_server = std::make_unique<LocalServiceImpl>(
        self.get(), id, std::move(delegate_ptr), std::move(service_iface));
    service_server->set_error_handler(connection_error_cb);
    self->services_[id] = std::move(service_server);

    callback(Status());
  };

  gatt()->RegisterService(std::move(service), std::move(id_cb),
                          std::move(read_handler), std::move(write_handler),
                          std::move(ccc_callback));
}

void GattServerServer::OnReadRequest(::btlib::gatt::IdType service_id,
                                     ::btlib::gatt::IdType id, uint16_t offset,
                                     ::btlib::gatt::ReadResponder responder) {
  auto iter = services_.find(service_id);
  if (iter == services_.end()) {
    responder(::btlib::att::ErrorCode::kUnlikelyError,
              ::btlib::common::BufferView());
    return;
  }

  auto cb = [responder = std::move(responder)](fidl::VectorPtr<uint8_t> value,
                                               auto error_code) {
    responder(GattErrorCodeFromFidl(error_code, true /* is_read */),
              ::btlib::common::BufferView(value->data(), value->size()));
  };

  auto* delegate = iter->second->delegate();
  ZX_DEBUG_ASSERT(delegate);
  delegate->OnReadValue(id, offset, fxl::MakeCopyable(std::move(cb)));
}

void GattServerServer::OnWriteRequest(::btlib::gatt::IdType service_id,
                                      ::btlib::gatt::IdType id, uint16_t offset,
                                      const ::btlib::common::ByteBuffer& value,
                                      ::btlib::gatt::WriteResponder responder) {
  auto iter = services_.find(service_id);
  if (iter == services_.end()) {
    responder(::btlib::att::ErrorCode::kUnlikelyError);
    return;
  }

  auto fidl_value = fxl::To<fidl::VectorPtr<uint8_t>>(value);
  auto* delegate = iter->second->delegate();
  ZX_DEBUG_ASSERT(delegate);

  if (!responder) {
    delegate->OnWriteWithoutResponse(id, offset, std::move(fidl_value));
    return;
  }

  auto cb = [responder = std::move(responder)](auto error_code) {
    responder(GattErrorCodeFromFidl(error_code, false /* is_read */));
  };

  delegate->OnWriteValue(id, offset, std::move(fidl_value),
                         fxl::MakeCopyable(std::move(cb)));
}

void GattServerServer::OnCharacteristicConfig(::btlib::gatt::IdType service_id,
                                              ::btlib::gatt::IdType chrc_id,
                                              const std::string& peer_id,
                                              bool notify, bool indicate) {
  auto iter = services_.find(service_id);
  if (iter != services_.end()) {
    auto* delegate = iter->second->delegate();
    ZX_DEBUG_ASSERT(delegate);
    delegate->OnCharacteristicConfiguration(chrc_id, peer_id, notify, indicate);
  }
}

}  // namespace bthost
