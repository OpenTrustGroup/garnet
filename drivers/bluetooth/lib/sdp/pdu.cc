// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/sdp/pdu.h"

#include "garnet/drivers/bluetooth/lib/common/log.h"
#include "garnet/drivers/bluetooth/lib/common/packet_view.h"
#include "garnet/drivers/bluetooth/lib/common/slab_allocator.h"
#include "garnet/drivers/bluetooth/lib/sdp/status.h"

#include <endian.h>

namespace btlib {

using common::BufferView;
using common::ByteBuffer;
using common::HostError;
using common::MutableByteBuffer;

namespace sdp {

namespace {

// Min size is Sequence uint8 (2 bytes) + uint16_t (3 bytes)
// See description of AttributeIDList in ServiceAttribute transaction
// Spec v5.0, Vol 3, Part B, Sec 4.6.1
constexpr size_t kMinAttributeIDListBytes = 5;

constexpr size_t kMaxServiceSearchSize = 12;

// Validates continuation state in |buf|, which should be the configuration
// state bytes of a PDU.
// Returns true if the continuation state is valid here, false otherwise.
// Sets |out| to point to it if present and valid.
bool ValidContinuationState(const ByteBuffer& buf, BufferView* out) {
  ZX_DEBUG_ASSERT(out);
  if (buf.size() == 0) {
    return false;
  }
  uint8_t len = buf[0];
  if (len == 0) {
    *out = BufferView();
    return true;
  }
  if (len >= kMaxContStateLength || len > (buf.size() - 1)) {
    return false;
  }
  *out = buf.view(1, len);
  return true;
}

common::MutableByteBufferPtr GetNewPDU(OpCode pdu_id, TransactionId tid,
                                       uint16_t param_length) {
  auto ptr = common::NewSlabBuffer(sizeof(Header) + param_length);
  if (!ptr) {
    return nullptr;
  }
  common::MutablePacketView<Header> packet(ptr.get(), param_length);
  packet.mutable_header()->pdu_id = pdu_id;
  packet.mutable_header()->tid = htobe16(tid);
  packet.mutable_header()->param_length = htobe16(param_length);
  return ptr;
}

// Parses an Attribute ID List sequence where every element is either:
// - 16-bit unsigned integer representing a specific Attribute ID
// - 32-bit unsigned integer which the high order 16-bits represent a
//   beginning attribute ID and the low order 16-bits represent a
//   ending attribute ID of a range.
// Returns the number of bytes taken by the list, or zero if an error
// occurred (wrong order, wrong format).
size_t ReadAttributeIDList(const ByteBuffer& buf,
                           std::list<AttributeRange>* attribute_ranges) {
  DataElement attribute_list_elem;
  size_t elem_size = DataElement::Read(&attribute_list_elem, buf);
  if ((elem_size == 0) ||
      (attribute_list_elem.type() != DataElement::Type::kSequence)) {
    bt_log(SPEW, "sdp", "failed to parse attribute ranges, or not a sequence");
    return 0;
  }
  uint16_t last_attr = 0x0000;
  const DataElement* it = attribute_list_elem.At(0);
  for (size_t i = 0; it != nullptr; it = attribute_list_elem.At(++i)) {
    if (it->type() != DataElement::Type::kUnsignedInt) {
      bt_log(SPEW, "sdp", "attribute range sequence invalid element type");
      attribute_ranges->clear();
      return 0;
    }
    if (it->size() == DataElement::Size::kTwoBytes) {
      uint16_t single_attr_id = *(it->Get<uint16_t>());
      if (single_attr_id < last_attr) {
        attribute_ranges->clear();
        return 0;
      }
      attribute_ranges->emplace_back(single_attr_id, single_attr_id);
      last_attr = single_attr_id;
    } else if (it->size() == DataElement::Size::kFourBytes) {
      uint32_t attr_range = *(it->Get<uint32_t>());
      uint16_t start_id = attr_range >> 16;
      uint16_t end_id = attr_range & 0xFFFF;
      if ((start_id < last_attr) || (end_id < start_id)) {
        attribute_ranges->clear();
        return 0;
      }
      attribute_ranges->emplace_back(start_id, end_id);
      last_attr = end_id;
    } else {
      attribute_ranges->clear();
      return 0;
    }
  }
  return elem_size;
}

void AddToAttributeRanges(std::list<AttributeRange>* ranges, AttributeId start,
                          AttributeId end) {
  auto it = ranges->begin();
  // Put the range in the list (possibly overlapping other ranges), with the
  // start in order.
  for (; it != ranges->end(); ++it) {
    if (start < it->start) {
      // This is where it should go.
      ranges->emplace(it, start, end);
    }
  }
  if (it == ranges->end()) {
    // It must be on the end.
    ranges->emplace_back(start, end);
  }
  // Merge any overlapping or adjacent ranges with no gaps.
  for (it = ranges->begin(); it != ranges->end();) {
    auto next = it;
    next++;
    if (next == ranges->end()) {
      return;
    }
    if (it->end >= (next->start - 1)) {
      next->start = it->start;
      if (next->end < it->end) {
        next->end = it->end;
      }
      it = ranges->erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace

Request::Request() { cont_state_.Fill(0); }

void Request::SetContinuationState(const ByteBuffer& buf) {
  ZX_DEBUG_ASSERT(buf.size() < kMaxContStateLength);
  cont_state_[0] = buf.size();
  if (cont_state_[0] == 0) {
    return;
  }
  auto v = cont_state_.mutable_view(sizeof(uint8_t));
  size_t copied = buf.Copy(&v);
  ZX_DEBUG_ASSERT(copied == buf.size());
}

bool Request::ParseContinuationState(const ByteBuffer& buf) {
  BufferView view;
  if (!ValidContinuationState(buf, &view)) {
    return false;
  }
  SetContinuationState(view);
  return true;
}

size_t Request::WriteContinuationState(MutableByteBuffer* buf) const {
  ZX_DEBUG_ASSERT(buf->size() > cont_info_size());
  size_t written_size = sizeof(uint8_t) + cont_info_size();
  buf->Write(cont_state_.view(0, written_size));
  return written_size;
}

Status ErrorResponse::Parse(const ByteBuffer& buf) {
  if (complete()) {
    return Status(HostError::kNotReady);
  }
  if (buf.size() != sizeof(ErrorCode)) {
    return Status(HostError::kPacketMalformed);
  }
  error_code_ = ErrorCode(betoh16(buf.As<uint16_t>()));
  return Status();
}

common::MutableByteBufferPtr ErrorResponse::GetPDU(uint16_t, TransactionId tid,
                                                   const ByteBuffer&) const {
  auto ptr = GetNewPDU(kErrorResponse, tid, sizeof(ErrorCode));
  size_t written = sizeof(Header);

  ptr->WriteObj(htobe16(static_cast<uint16_t>(error_code_)), written);

  return ptr;
}

ServiceSearchRequest::ServiceSearchRequest()
    : Request(), max_service_record_count_(0xFFFF) {}

ServiceSearchRequest::ServiceSearchRequest(const common::ByteBuffer& params)
    : ServiceSearchRequest() {
  DataElement search_pattern;
  size_t read_size = DataElement::Read(&search_pattern, params);
  if ((read_size == 0) ||
      (search_pattern.type() != DataElement::Type::kSequence)) {
    bt_log(SPEW, "sdp", "Failed to read search pattern");
    return;
  }
  size_t min_size = read_size + sizeof(uint16_t) + sizeof(uint8_t);
  if (params.size() < min_size) {
    bt_log(SPEW, "sdp", "Params too small: %d < %d", params.size(), min_size);
    return;
  }
  const DataElement* it;
  size_t count;
  for (count = 0, it = search_pattern.At(count); it != nullptr;
       it = search_pattern.At(++count)) {
    if ((count >= kMaxServiceSearchSize) ||
        (it->type() != DataElement::Type::kUuid)) {
      bt_log(SPEW, "sdp", "Search pattern invalid");
      service_search_pattern_.clear();
      return;
    }
    service_search_pattern_.emplace(*(it->Get<common::UUID>()));
  }
  if (count == 0) {
    bt_log(SPEW, "sdp", "Search pattern invalid: no records");
    return;
  }
  max_service_record_count_ = betoh16(params.view(read_size).As<uint16_t>());
  read_size += sizeof(uint16_t);
  if (!ParseContinuationState(params.view(read_size))) {
    service_search_pattern_.clear();
    return;
  }
  ZX_DEBUG_ASSERT(valid());
}

bool ServiceSearchRequest::valid() const {
  return service_search_pattern_.size() > 0 &&
         service_search_pattern_.size() <= kMaxServiceSearchSize &&
         max_service_record_count_ > 0;
}

common::ByteBufferPtr ServiceSearchRequest::GetPDU(TransactionId tid) const {
  if (!valid()) {
    return nullptr;
  }
  size_t size = sizeof(uint16_t) + sizeof(uint8_t) + cont_info_size();

  std::vector<DataElement> pattern(service_search_pattern_.size());
  size_t i = 0;
  for (auto& it : service_search_pattern_) {
    pattern.at(i).Set(it);
    i++;
  }
  DataElement search_pattern(std::move(pattern));

  size += search_pattern.WriteSize();
  auto buf = GetNewPDU(kServiceSearchRequest, tid, size);
  size_t written = sizeof(Header);

  // Write ServiceSearchPattern
  auto write_view = buf->mutable_view(written);
  written += search_pattern.Write(&write_view);
  // Write MaxServiceRecordCount
  buf->WriteObj(htobe16(max_service_record_count_), written);
  written += sizeof(uint16_t);
  // Write Continuation State
  write_view = buf->mutable_view(written);
  written += WriteContinuationState(&write_view);

  ZX_DEBUG_ASSERT(written == sizeof(Header) + size);
  return buf;
}

ServiceSearchResponse::ServiceSearchResponse()
    : total_service_record_count_(0) {}

bool ServiceSearchResponse::complete() const {
  return total_service_record_count_ == service_record_handle_list_.size();
}

const common::BufferView ServiceSearchResponse::ContinuationState() const {
  if (!continuation_state_) {
    return common::BufferView();
  }
  return continuation_state_->view();
}

Status ServiceSearchResponse::Parse(const common::ByteBuffer& buf) {
  if (complete() && total_service_record_count_ != 0) {
    // This response was previously complete and non-empty.
    bt_log(SPEW, "sdp", "Can't parse into a complete response");
    return Status(HostError::kNotReady);
  }
  if (buf.size() < (2 * sizeof(uint16_t))) {
    bt_log(SPEW, "sdp", "Packet too small to parse");
    return Status(HostError::kPacketMalformed);
  }

  uint16_t total_service_record_count = betoh16(buf.As<uint16_t>());
  size_t read_size = sizeof(uint16_t);
  if (total_service_record_count_ != 0 &&
      total_service_record_count_ != total_service_record_count) {
    bt_log(SPEW, "sdp", "Continuing packet has different record count");
    return Status(HostError::kPacketMalformed);
  }
  total_service_record_count_ = total_service_record_count;

  uint16_t record_count = betoh16(buf.view(read_size).As<uint16_t>());
  read_size += sizeof(uint16_t);
  if ((buf.size() - read_size - sizeof(uint8_t)) <
      (sizeof(ServiceHandle) * record_count)) {
    bt_log(SPEW, "sdp", "Packet too small for %d records", record_count);
    return Status(HostError::kPacketMalformed);
  }
  for (uint16_t i = 0; i < record_count; i++) {
    auto view = buf.view(read_size + i * sizeof(ServiceHandle));
    service_record_handle_list_.emplace_back(betoh32(view.As<uint32_t>()));
  }
  read_size += sizeof(ServiceHandle) * record_count;
  common::BufferView cont_state_view;
  if (!ValidContinuationState(buf.view(read_size), &cont_state_view)) {
    bt_log(SPEW, "sdp", "Failed to find continuation state");
    return Status(HostError::kPacketMalformed);
  }
  if (cont_state_view.size() == 0) {
    continuation_state_ = nullptr;
  } else {
    continuation_state_ =
        std::make_unique<common::DynamicByteBuffer>(cont_state_view);
  }
  return Status();
}

// Continuation state: Index of the start record for the continued response.
common::MutableByteBufferPtr ServiceSearchResponse::GetPDU(
    uint16_t max, TransactionId tid,
    const common::ByteBuffer& cont_state) const {
  if (!complete()) {
    return nullptr;
  }
  // We never generate continuation for ServiceSearchResponses.
  // TODO(jamuraa): do we need to be concerned with MTU?
  if (cont_state.size() > 0) {
    return nullptr;
  }

  uint16_t response_record_count = total_service_record_count_;
  if (max < response_record_count) {
    bt_log(SPEW, "sdp", "Limit ServiceSearchResponse to %d records", max);
    response_record_count = max;
  }

  size_t size = (2 * sizeof(uint16_t)) +
                (response_record_count * sizeof(ServiceHandle)) +
                sizeof(uint8_t);

  auto buf = GetNewPDU(kServiceSearchResponse, tid, size);
  if (!buf) {
    return buf;
  }

  size_t written = sizeof(Header);
  // The total service record count and current service record count is the
  // same.
  buf->WriteObj(htobe16(response_record_count), written);
  written += sizeof(uint16_t);
  buf->WriteObj(htobe16(response_record_count), written);
  written += sizeof(uint16_t);

  for (size_t i = 0; i < response_record_count; i++) {
    buf->WriteObj(htobe32(service_record_handle_list_.at(i)), written);
    written += sizeof(ServiceHandle);
  }
  // There's no continuation state. Write the InfoLength.
  buf->WriteObj(static_cast<uint8_t>(0), written);
  written += sizeof(uint8_t);
  ZX_DEBUG_ASSERT(written == sizeof(Header) + size);
  return buf;
}


ServiceAttributeRequest::ServiceAttributeRequest()
    : service_record_handle_(0), max_attribute_byte_count_(0xFFFF) {}

ServiceAttributeRequest::ServiceAttributeRequest(
    const common::ByteBuffer& params) {
  if (params.size() < sizeof(uint32_t) + sizeof(uint16_t)) {
    bt_log(SPEW, "sdp", "packet too small for ServiceAttributeRequest");
    max_attribute_byte_count_ = 0;
    return;
  }

  service_record_handle_ = betoh32(params.As<uint32_t>());
  size_t read_size = sizeof(uint32_t);
  max_attribute_byte_count_ = betoh16(params.view(read_size).As<uint16_t>());
  if (max_attribute_byte_count_ < kMinMaximumAttributeByteCount) {
    bt_log(SPEW, "sdp", "max attribute byte count too small (%d < %d)",
           max_attribute_byte_count_, kMinMaximumAttributeByteCount);
    return;
  }
  read_size += sizeof(uint16_t);

  size_t elem_size =
      ReadAttributeIDList(params.view(read_size), &attribute_ranges_);
  if (elem_size == 0) {
    max_attribute_byte_count_ = 0;
    return;
  }
  read_size += elem_size;

  if (!ParseContinuationState(params.view(read_size))) {
    attribute_ranges_.clear();
    return;
  }
  ZX_DEBUG_ASSERT(valid());
}

bool ServiceAttributeRequest::valid() const {
  return (max_attribute_byte_count_ >= kMinMaximumAttributeByteCount) &&
         (attribute_ranges_.size() > 0);
}

common::ByteBufferPtr ServiceAttributeRequest::GetPDU(TransactionId tid) const {
  if (!valid()) {
    return nullptr;
  }

  size_t size = sizeof(ServiceHandle) + sizeof(uint16_t) + sizeof(uint8_t) +
                cont_info_size();

  std::vector<DataElement> attribute_list(attribute_ranges_.size());
  size_t idx = 0;
  for (const auto& it : attribute_ranges_) {
    if (it.start == it.end) {
      attribute_list.at(idx).Set<uint16_t>(it.start);
    } else {
      uint32_t attr_range = (static_cast<uint32_t>(it.start) << 16);
      attr_range |= it.end;
      attribute_list.at(idx).Set<uint32_t>(attr_range);
    }
    idx++;
  }

  DataElement attribute_list_elem(std::move(attribute_list));
  size += attribute_list_elem.WriteSize();

  auto buf = GetNewPDU(kServiceAttributeRequest, tid, size);
  if (!buf) {
    return nullptr;
  }
  size_t written = sizeof(Header);

  buf->WriteObj(htobe32(service_record_handle_), written);
  written += sizeof(uint32_t);

  buf->WriteObj(htobe16(max_attribute_byte_count_), written);
  written += sizeof(uint16_t);

  auto mut_view = buf->mutable_view(written);
  written += attribute_list_elem.Write(&mut_view);

  mut_view = buf->mutable_view(written);
  written += WriteContinuationState(&mut_view);
  ZX_DEBUG_ASSERT(written == sizeof(Header) + size);
  return buf;
}

void ServiceAttributeRequest::AddAttribute(AttributeId id) {
  AddToAttributeRanges(&attribute_ranges_, id, id);
}

void ServiceAttributeRequest::AddAttributeRange(AttributeId start,
                                                AttributeId end) {
  AddToAttributeRanges(&attribute_ranges_, start, end);
}

ServiceAttributeResponse::ServiceAttributeResponse() {}

const common::BufferView ServiceAttributeResponse::ContinuationState() const {
  if (!continuation_state_) {
    return common::BufferView();
  }
  return continuation_state_->view();
}

bool ServiceAttributeResponse::complete() const { return !continuation_state_; }

Status ServiceAttributeResponse::Parse(const common::ByteBuffer& buf) {
  if (complete() && attributes_.size() != 0) {
    // This response was previously complete and non-empty
    bt_log(SPEW, "sdp", "Can't parse into a complete response");
    // partial_response_ is already empty
    return Status(common::HostError::kNotReady);
  }

  if (buf.size() < sizeof(uint16_t)) {
    bt_log(SPEW, "sdp", "Packet too small to parse");
    return Status(common::HostError::kPacketMalformed);
  }

  uint16_t attribute_list_byte_count = betoh16(buf.As<uint16_t>());
  size_t read_size = sizeof(uint16_t);
  if (buf.view(read_size).size() <
      attribute_list_byte_count + sizeof(uint8_t)) {
    bt_log(SPEW, "sdp", "Not enough bytes in rest of packet");
    return Status(common::HostError::kPacketMalformed);
  }
  // Check to see if there's continuation.
  common::BufferView cont_state_view;
  if (!ValidContinuationState(buf.view(read_size + attribute_list_byte_count),
                              &cont_state_view)) {
    bt_log(SPEW, "sdp", "Continutation state is not valid");
    return Status(common::HostError::kPacketMalformed);
  }

  if (cont_state_view.size() == 0) {
    continuation_state_ = nullptr;
  } else {
    continuation_state_ = common::NewSlabBuffer(cont_state_view.size());
    continuation_state_->Write(cont_state_view);
  }

  auto attribute_list_bytes = buf.view(read_size, attribute_list_byte_count);
  if (partial_response_ || ContinuationState().size()) {
    // Append to the incomplete buffer.
    size_t new_partial_size = attribute_list_byte_count;
    if (partial_response_) {
      new_partial_size += partial_response_->size();
    }
    auto new_partial = common::NewSlabBuffer(new_partial_size);
    if (partial_response_) {
      new_partial->Write(partial_response_->view());
      new_partial->Write(attribute_list_bytes, partial_response_->size());
    } else {
      new_partial->Write(attribute_list_bytes);
    }
    partial_response_ = std::move(new_partial);
    if (continuation_state_) {
      // This is incomplete, we can't parse it yet.
      bt_log(SPEW, "sdp", "Continutation state, returning in progress");
      return Status(common::HostError::kInProgress);
    }
    attribute_list_bytes = partial_response_->view();
  }

  DataElement attribute_list;
  size_t elem_size = DataElement::Read(&attribute_list, attribute_list_bytes);
  if ((elem_size == 0) ||
      (attribute_list.type() != DataElement::Type::kSequence)) {
    bt_log(SPEW, "sdp",
           "Couldn't parse attribute list or it wasn't a sequence");
    return Status(common::HostError::kPacketMalformed);
  }

  // Data Element sequence containing alternating attribute id and attribute
  // value pairs.  Only the requested attributes that are present are included.
  // They are sorted in ascenting attribute ID order.
  AttributeId last_id = 0;
  size_t idx = 0;
  for (auto* it = attribute_list.At(0); it != nullptr;
       it = attribute_list.At(idx)) {
    auto* val = attribute_list.At(idx + 1);
    if ((it->type() != DataElement::Type::kUnsignedInt) || (val == nullptr)) {
      attributes_.clear();
      return Status(common::HostError::kPacketMalformed);
    }
    AttributeId id = *(it->Get<uint16_t>());
    if (id < last_id) {
      attributes_.clear();
      return Status(common::HostError::kPacketMalformed);
    }
    attributes_.emplace(id, val->Clone());
    last_id = id;
    idx += 2;
  }
  return Status();
}

// Continuation state: index of # of bytes into the attribute list element
common::MutableByteBufferPtr ServiceAttributeResponse::GetPDU(
    uint16_t max, TransactionId tid,
    const common::ByteBuffer& cont_state) const {
  if (!complete()) {
    return nullptr;
  }
  // If there's continuation state, it's the # of bytes previously written
  // of the attribute list.
  uint32_t bytes_skipped = 0;
  if (cont_state.size() == sizeof(uint32_t)) {
    bytes_skipped = betoh32(cont_state.As<uint32_t>());
  } else if (cont_state.size() != 0) {
    // We don't generate continuation states of any other length.
    return nullptr;
  }

  // Returned in pairs of (attribute id, attribute value)
  std::vector<DataElement> list;
  list.reserve(2 * attributes_.size());
  for (const auto& it : attributes_) {
    list.emplace_back(static_cast<uint16_t>(it.first));
    list.emplace_back(it.second.Clone());
  }
  DataElement list_elem(std::move(list));
  uint16_t attribute_list_byte_count = list_elem.WriteSize() - bytes_skipped;
  uint8_t info_length = 0;
  if (attribute_list_byte_count > max) {
    attribute_list_byte_count = max;
    info_length = sizeof(uint32_t);
  }

  size_t size = sizeof(uint16_t) + attribute_list_byte_count + sizeof(uint8_t) +
                info_length;
  auto buf = GetNewPDU(kServiceAttributeResponse, tid, size);
  if (!buf) {
    return nullptr;
  }
  size_t written = sizeof(Header);

  buf->WriteObj(htobe16(attribute_list_byte_count), written);
  written += sizeof(uint16_t);

  auto attribute_list_bytes = common::NewSlabBuffer(list_elem.WriteSize());
  list_elem.Write(attribute_list_bytes.get());
  buf->Write(
      attribute_list_bytes->view(bytes_skipped, attribute_list_byte_count),
      written);
  written += attribute_list_byte_count;

  // Continuation state
  buf->WriteObj(info_length, written);
  written += sizeof(uint8_t);
  if (info_length > 0) {
    bytes_skipped += attribute_list_byte_count;
    buf->WriteObj(htobe32(bytes_skipped), written);
    written += sizeof(uint32_t);
  }
  ZX_DEBUG_ASSERT(written == sizeof(Header) + size);
  return buf;
}

ServiceSearchAttributeRequest::ServiceSearchAttributeRequest()
    : Request(), max_attribute_byte_count_(0xFFFF) {}

ServiceSearchAttributeRequest::ServiceSearchAttributeRequest(
    const common::ByteBuffer& params) {
  DataElement search_pattern;
  size_t read_size = DataElement::Read(&search_pattern, params);
  if ((read_size == 0) ||
      (search_pattern.type() != DataElement::Type::kSequence)) {
    bt_log(SPEW, "sdp", "failed to read search pattern");
    max_attribute_byte_count_ = 0;
    return;
  }
  // Minimum size is ServiceSearchPattern (varies, above) +
  // MaximumAttributeByteCount + AttributeIDList + Cont State (uint8)
  if (params.size() < read_size + sizeof(max_attribute_byte_count_) +
                          kMinAttributeIDListBytes + sizeof(uint8_t)) {
    bt_log(SPEW, "sdp", "packet too small for ServiceSearchAttributeRequest");
    max_attribute_byte_count_ = 0;
    return;
  }

  const DataElement* it;
  size_t count;
  for (count = 0, it = search_pattern.At(count); it != nullptr;
       it = search_pattern.At(++count)) {
    if ((count >= kMaxServiceSearchSize) ||
        (it->type() != DataElement::Type::kUuid)) {
      bt_log(SPEW, "sdp", "search pattern is invalid");
      service_search_pattern_.clear();
      return;
    }
    service_search_pattern_.emplace(*(it->Get<common::UUID>()));
  }
  if (count == 0) {
    bt_log(SPEW, "sdp", "no elements in search pattern");
    max_attribute_byte_count_ = 0;
    return;
  }

  max_attribute_byte_count_ = betoh16(params.view(read_size).As<uint16_t>());
  if (max_attribute_byte_count_ < kMinMaximumAttributeByteCount) {
    bt_log(SPEW, "sdp", "max attribute byte count to small (%d)",
           max_attribute_byte_count_);
    max_attribute_byte_count_ = 0;
    return;
  }
  read_size += sizeof(uint16_t);

  size_t elem_size =
      ReadAttributeIDList(params.view(read_size), &attribute_ranges_);
  if (elem_size == 0) {
    max_attribute_byte_count_ = 0;
    return;
  }
  read_size += elem_size;

  if (!ParseContinuationState(params.view(read_size))) {
    attribute_ranges_.clear();
    return;
  }

  bt_log(SPEW, "sdp",
         "parsed: %d search uuids, %d max bytes, %d attribute ranges",
         service_search_pattern_.size(), max_attribute_byte_count_,
         attribute_ranges_.size());

  ZX_DEBUG_ASSERT(valid());
}

bool ServiceSearchAttributeRequest::valid() const {
  return (max_attribute_byte_count_ > kMinMaximumAttributeByteCount) &&
         (service_search_pattern_.size() > 0) &&
         (service_search_pattern_.size() <= kMaxServiceSearchSize) &&
         (attribute_ranges_.size() > 0);
}

common::ByteBufferPtr ServiceSearchAttributeRequest::GetPDU(
    TransactionId tid) const {
  if (!valid()) {
    return nullptr;
  }

  // Size of fixed length components: MaxAttributesByteCount, continuation info
  size_t size = sizeof(max_attribute_byte_count_) + cont_info_size() + 1;

  std::vector<DataElement> attribute_list(attribute_ranges_.size());
  size_t idx = 0;
  for (const auto& it : attribute_ranges_) {
    if (it.start == it.end) {
      attribute_list.at(idx).Set<uint16_t>(it.start);
    } else {
      uint32_t attr_range = (static_cast<uint32_t>(it.start) << 16);
      attr_range |= it.end;
      attribute_list.at(idx).Set<uint32_t>(attr_range);
    }
    idx++;
  }

  DataElement attribute_list_elem(std::move(attribute_list));
  size += attribute_list_elem.WriteSize();

  std::vector<DataElement> pattern(service_search_pattern_.size());
  size_t i = 0;
  for (const auto& it : service_search_pattern_) {
    pattern.at(i).Set<common::UUID>(it);
    i++;
  }
  DataElement search_pattern(std::move(pattern));
  size += search_pattern.WriteSize();

  auto buf = GetNewPDU(kServiceSearchAttributeRequest, tid, size);
  if (!buf) {
    return nullptr;
  }
  size_t written = sizeof(Header);

  auto mut_view = buf->mutable_view(written);
  written += search_pattern.Write(&mut_view);

  buf->WriteObj(htobe16(max_attribute_byte_count_), written);
  written += sizeof(uint16_t);

  mut_view = buf->mutable_view(written);
  written += attribute_list_elem.Write(&mut_view);

  mut_view = buf->mutable_view(written);
  written += WriteContinuationState(&mut_view);
  ZX_DEBUG_ASSERT(written == sizeof(Header) + size);
  return buf;
}

void ServiceSearchAttributeRequest::AddAttribute(AttributeId id) {
  AddToAttributeRanges(&attribute_ranges_, id, id);
}

void ServiceSearchAttributeRequest::AddAttributeRange(AttributeId start,
                                                      AttributeId end) {
  AddToAttributeRanges(&attribute_ranges_, start, end);
}

ServiceSearchAttributeResponse::ServiceSearchAttributeResponse() {}

const common::BufferView ServiceSearchAttributeResponse::ContinuationState()
    const {
  if (!continuation_state_) {
    return common::BufferView();
  }
  return continuation_state_->view();
}

bool ServiceSearchAttributeResponse::complete() const {
  return !continuation_state_;
}

Status ServiceSearchAttributeResponse::Parse(const common::ByteBuffer& buf) {
  if (complete() && attribute_lists_.size() != 0) {
    // This response was previously complete and non-empty
    bt_log(SPEW, "sdp", "can't parse into a complete response");
    ZX_DEBUG_ASSERT(!partial_response_);
    return Status(common::HostError::kNotReady);
  }

  // Minimum size is an AttributeListsByteCount, an empty AttributeLists
  // (two bytes) and an empty continutation state (1 byte)
  // of AttributeLists
  if (buf.size() < sizeof(uint16_t) + 3) {
    bt_log(SPEW, "sdp", "packet too small to parse");
    return Status(common::HostError::kPacketMalformed);
  }

  uint16_t attribute_lists_byte_count = betoh16(buf.As<uint16_t>());
  size_t read_size = sizeof(uint16_t);
  if (buf.view(read_size).size() <
      attribute_lists_byte_count + sizeof(uint8_t)) {
    bt_log(SPEW, "sdp", "not enough bytes in rest of packet as indicated");
    return Status(common::HostError::kPacketMalformed);
  }
  // Check to see if there's continuation.
  common::BufferView cont_state_view;
  if (!ValidContinuationState(buf.view(read_size + attribute_lists_byte_count),
                              &cont_state_view)) {
    bt_log(SPEW, "sdp", "continutation state is not valid");
    return Status(common::HostError::kPacketMalformed);
  }

  if (cont_state_view.size() == 0) {
    continuation_state_ = nullptr;
  } else {
    continuation_state_ = common::NewSlabBuffer(cont_state_view.size());
    continuation_state_->Write(cont_state_view);
  }

  auto attribute_lists_bytes = buf.view(read_size, attribute_lists_byte_count);
  if (partial_response_ || ContinuationState().size()) {
    // Append to the incomplete buffer.
    size_t new_partial_size = attribute_lists_byte_count;
    if (partial_response_) {
      new_partial_size += partial_response_->size();
    }
    auto new_partial = common::NewSlabBuffer(new_partial_size);
    if (partial_response_) {
      new_partial->Write(partial_response_->view());
      new_partial->Write(attribute_lists_bytes, partial_response_->size());
    } else {
      new_partial->Write(attribute_lists_bytes);
    }
    partial_response_ = std::move(new_partial);
    if (continuation_state_) {
      // This is incomplete, we can't parse it yet.
      bt_log(SPEW, "sdp", "continutation state found, returning in progress");
      return Status(common::HostError::kInProgress);
    }
    attribute_lists_bytes = partial_response_->view();
  }

  DataElement attribute_lists;
  size_t elem_size = DataElement::Read(&attribute_lists, attribute_lists_bytes);
  if ((elem_size == 0) ||
      (attribute_lists.type() != DataElement::Type::kSequence)) {
    bt_log(SPEW, "sdp", "couldn't parse attribute lists or wasn't a sequence");
    return Status(common::HostError::kPacketMalformed);
  }
  bt_log(SPEW, "sdp", "parsed AttributeLists: %s",
         attribute_lists.ToString().c_str());

  // Data Element sequence containing alternating attribute id and attribute
  // value pairs.  Only the requested attributes that are present are included.
  // They are sorted in ascenting attribute ID order.
  size_t list_idx = 0;
  for (auto* list_it = attribute_lists.At(0); list_it != nullptr;
       list_it = attribute_lists.At(++list_idx)) {
    if ((list_it->type() != DataElement::Type::kSequence)) {
      bt_log(SPEW, "sdp", "list %d wasn't a sequence", list_idx);
      return Status(common::HostError::kPacketMalformed);
    }
    attribute_lists_.emplace(list_idx, std::map<AttributeId, DataElement>());
    AttributeId last_id = 0;
    size_t idx = 0;
    for (auto* it = list_it->At(0); it != nullptr; it = list_it->At(idx)) {
      auto* val = list_it->At(idx + 1);
      if ((it->type() != DataElement::Type::kUnsignedInt) || (val == nullptr)) {
        attribute_lists_.clear();
        bt_log(SPEW, "sdp", "attribute isn't a ptr or doesn't exist");
        return Status(common::HostError::kPacketMalformed);
      }
      bt_log(SPEW, "sdp", "adding %d:%s = %s", list_idx, it->ToString().c_str(),
             val->ToString().c_str());
      AttributeId id = *(it->Get<uint16_t>());
      if (id < last_id) {
        attribute_lists_.clear();
        bt_log(SPEW, "sdp", "attribute ids are in wrong order");
        return Status(common::HostError::kPacketMalformed);
      }
      attribute_lists_.at(list_idx).emplace(id, val->Clone());
      last_id = id;
      idx += 2;
    }
  }
  partial_response_ = nullptr;
  return Status();
}

void ServiceSearchAttributeResponse::SetAttribute(uint32_t idx, AttributeId id,
                                                  DataElement value) {
  if (attribute_lists_.find(idx) == attribute_lists_.end()) {
    attribute_lists_.emplace(idx, std::map<AttributeId, DataElement>());
  }
  attribute_lists_[idx].emplace(id, std::move(value));
}

// Continuation state: index of # of bytes into the attribute list element
common::MutableByteBufferPtr ServiceSearchAttributeResponse::GetPDU(
    uint16_t max, TransactionId tid,
    const common::ByteBuffer& cont_state) const {
  if (!complete()) {
    return nullptr;
  }
  // If there's continuation state, it's the # of bytes previously written
  // of the attribute list.
  uint32_t bytes_skipped = 0;
  if (cont_state.size() == sizeof(uint32_t)) {
    bytes_skipped = betoh32(cont_state.As<uint32_t>());
  } else if (cont_state.size() != 0) {
    // We don't generate continuation states of any other length.
    return nullptr;
  }

  std::vector<DataElement> lists;
  lists.reserve(attribute_lists_.size());
  for (const auto& it : attribute_lists_) {
    // Returned in pairs of (attribute id, attribute value)
    std::vector<DataElement> list;
    list.reserve(2 * it.second.size());
    for (const auto& elem_it : it.second) {
      list.emplace_back(static_cast<uint16_t>(elem_it.first));
      list.emplace_back(elem_it.second.Clone());
    }

    lists.emplace_back(std::move(list));
  }

  DataElement list_elem(std::move(lists));
  uint16_t attribute_lists_byte_count = list_elem.WriteSize() - bytes_skipped;
  uint8_t info_length = 0;
  if (attribute_lists_byte_count > max) {
    attribute_lists_byte_count = max;
    info_length = sizeof(uint32_t);
  }

  size_t size = sizeof(uint16_t) + attribute_lists_byte_count +
                sizeof(uint8_t) + info_length;
  auto buf = GetNewPDU(kServiceSearchAttributeResponse, tid, size);
  if (!buf) {
    return nullptr;
  }
  size_t written = sizeof(Header);

  buf->WriteObj(htobe16(attribute_lists_byte_count), written);
  written += sizeof(uint16_t);

  auto attribute_list_bytes = common::NewSlabBuffer(list_elem.WriteSize());
  list_elem.Write(attribute_list_bytes.get());
  buf->Write(
      attribute_list_bytes->view(bytes_skipped, attribute_lists_byte_count),
      written);
  written += attribute_lists_byte_count;

  // Continuation state
  buf->WriteObj(info_length, written);
  written += sizeof(uint8_t);
  if (info_length > 0) {
    bytes_skipped = bytes_skipped + attribute_lists_byte_count;
    buf->WriteObj(htobe32(bytes_skipped), written);
    written += sizeof(uint32_t);
  }
  ZX_DEBUG_ASSERT(written == sizeof(Header) + size);
  return buf;
}
}  // namespace sdp
}  // namespace btlib
