// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attribute.h"

namespace btlib {
namespace att {

AccessRequirements::AccessRequirements() : value_(0u) {}

AccessRequirements::AccessRequirements(bool encryption,
                                       bool authentication,
                                       bool authorization)
    : value_(kAttributePermissionBitAllowed) {
  if (encryption) {
    value_ |= kAttributePermissionBitEncryptionRequired;
  }
  if (authentication) {
    value_ |= kAttributePermissionBitAuthenticationRequired;
  }
  if (authorization) {
    value_ |= kAttributePermissionBitAuthorizationRequired;
  }
}

Attribute::Attribute(AttributeGrouping* group,
                     Handle handle,
                     const common::UUID& type,
                     const AccessRequirements& read_reqs,
                     const AccessRequirements& write_reqs)
    : group_(group),
      handle_(handle),
      type_(type),
      read_reqs_(read_reqs),
      write_reqs_(write_reqs) {
  ZX_DEBUG_ASSERT(group_);
  ZX_DEBUG_ASSERT(is_initialized());
}

Attribute::Attribute() : handle_(kInvalidHandle) {}

void Attribute::SetValue(const common::ByteBuffer& value) {
  ZX_DEBUG_ASSERT(value.size());
  ZX_DEBUG_ASSERT(value.size() <= kMaxAttributeValueLength);
  ZX_DEBUG_ASSERT(!write_reqs_.allowed());
  value_ = common::DynamicByteBuffer(value);
}

bool Attribute::ReadAsync(const std::string& peer_id,
                          uint16_t offset,
                          ReadResultCallback result_callback) const {
  if (!is_initialized() || !read_handler_)
    return false;

  if (!read_reqs_.allowed())
    return false;

  read_handler_(peer_id, handle_, offset, std::move(result_callback));
  return true;
}

bool Attribute::WriteAsync(const std::string& peer_id,
                           uint16_t offset,
                           const common::ByteBuffer& value,
                           WriteResultCallback result_callback) const {
  if (!is_initialized() || !write_handler_)
    return false;

  if (!write_reqs_.allowed())
    return false;

  write_handler_(peer_id, handle_, offset, std::move(value), std::move(result_callback));
  return true;
}

AttributeGrouping::AttributeGrouping(const common::UUID& group_type,
                                     Handle start_handle,
                                     size_t attr_count,
                                     const common::ByteBuffer& decl_value)
    : start_handle_(start_handle), active_(false) {
  ZX_DEBUG_ASSERT(start_handle_ != kInvalidHandle);
  ZX_DEBUG_ASSERT(kHandleMax - start_handle > attr_count);
  ZX_DEBUG_ASSERT(decl_value.size());

  end_handle_ = start_handle + attr_count;
  attributes_.reserve(attr_count + 1);

  // TODO(armansito): Allow callers to require at most encryption.
  attributes_.push_back(Attribute(
      this, start_handle, group_type,
      AccessRequirements(false, false, false),  // read allowed, no security
      AccessRequirements()));                   // write disallowed

  attributes_[0].SetValue(decl_value);
}

Attribute* AttributeGrouping::AddAttribute(
    const common::UUID& type,
    const AccessRequirements& read_reqs,
    const AccessRequirements& write_reqs) {
  if (complete())
    return nullptr;

  ZX_DEBUG_ASSERT(attributes_[attributes_.size() - 1].handle() < end_handle_);

  Handle handle = start_handle_ + attributes_.size();
  attributes_.push_back(Attribute(this, handle, type, read_reqs, write_reqs));

  return &attributes_[handle - start_handle_];
}

}  // namespace att
}  // namespace btlib
