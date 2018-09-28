// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_SDP_PDU_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_SDP_PDU_H_

#include "garnet/drivers/bluetooth/lib/sdp/sdp.h"

namespace btlib {
namespace sdp {

constexpr uint64_t kInvalidContState = 0xFFFFFFFF;

// Maximum length of continuation information is 16 bytes, and the InfoLength
// is one byte. See v5.0, Vol 3, Part B, Sec 4.3
constexpr size_t kMaxContStateLength = 17;

// Minimum length allowed by the Maximum Attribute Byte Count in
// ServiceAttribute and ServiceSearchAttribute requests
constexpr size_t kMinMaximumAttributeByteCount = 0x0007;

class Request {
 public:
  Request();
  virtual ~Request() = default;

  // Returns true if the request is valid.
  virtual bool valid() const = 0;

  // Gets a buffer containing the PDU representation of this request.
  // Returns nullptr if the request is not valid.
  virtual common::ByteBufferPtr GetPDU(TransactionId tid) const = 0;

  // Returns a view with the current continuation state.
  // In a response packet with more than one packet, this contains the most
  // recent continuaton state (so it can be read to request a continuation).
  const common::BufferView ContinuationState() const {
    return cont_state_.view(1, cont_info_size());
  }

  // Sets the continuation state for this request.
  void SetContinuationState(const common::ByteBuffer& buf);

 protected:
  // Parses the continuation state portion of a packet, which is in |buf|.
  // Returns true if the parsing succeeded.
  bool ParseContinuationState(const common::ByteBuffer& buf);

  // Writes the continuation state to |buf|, which must have at least
  // cont_info_size() + 1 bytes avaiable.
  size_t WriteContinuationState(common::MutableByteBuffer* buf) const;

  uint8_t cont_info_size() const { return cont_state_.data()[0]; }

 private:
  // Continuation information, including the length.
  common::StaticByteBuffer<kMaxContStateLength> cont_state_;
};

// SDP Response objects are used in two places:
//  - to construct a response for returning from a request on the server
//  - to receive responses from a server as a client, possibly building from
//    multiple response PDUs
class Response {
 public:
  // Returns true if these parameters represent a complete response.
  virtual bool complete() const = 0;

  // Returns the continuation state from a partial response, used to
  // make an additional request.  Returns an empty view if this packet
  // is complete.
  virtual const common::BufferView ContinuationState() const = 0;

  // Parses parameters from a PDU response, storing a partial result if
  // necessary.
  // Returns a success status if the parameters could ba parsed, or a status
  // containing:
  //  - kNotReady if this response is already complete.
  //  - kPacketMalformed: if the parameters couldn't be parsed.
  //  - kOutOfMemory: if memory isn't available to store a partial response.
  virtual Status Parse(const common::ByteBuffer& buf) = 0;

  // Returns a buffer containing the PDU representation of this response,
  // including the header.
  // |max| will control the maximum size of the parameters based on the
  // transaction type:
  //  - for ServiceSearchResponse, this should be the maximum count of records
  //    to be included.
  //  - for ServiceAttributeResponse or ServiceSearchAttributeResponse, this
  //    is the MaximumAttributeByteCount from the request
  // The buffer parameters will contain continuation state if it is not the
  // last valid packet representing a response.
  // If that continuation state is passed to this function with the same
  // |max_size| argument it will produce the next parameters of response.
  virtual common::MutableByteBufferPtr GetPDU(
      uint16_t max, TransactionId tid,
      const common::ByteBuffer& cont_state) const = 0;
};

// Error Response PDU, generated when the SDP server can't respond to a PDU
// because it is malformed or for another reason.
// See v5.0, Vol 3, Part B, 4.4.1
class ErrorResponse : public Response {
 public:
  ErrorResponse(ErrorCode code = ErrorCode::kReserved) : error_code_(code) {}
  // Response overrides.
  bool complete() const override { return error_code_ != ErrorCode::kReserved; }

  const common::BufferView ContinuationState() const override {
    // ErrorResponses never have continuation state.
    return common::BufferView();
  }

  Status Parse(const common::ByteBuffer& buf) override;

  // Note: |max_size| and |cont_state| are ignored.
  // Error Responses do not have a valid continuation.
  common::MutableByteBufferPtr GetPDU(
      uint16_t max, TransactionId tid,
      const common::ByteBuffer& cont_state) const override;

  ErrorCode error_code() const { return error_code_; }
  void set_error_code(ErrorCode code) { error_code_ = code; }

 private:
  ErrorCode error_code_;
};

// Used to locate service records that match a pattern.
// Note: there is no mechanism to retrieve all service records.
// See v5.0, Vol 3, Part B, 4.5.1
class ServiceSearchRequest : public Request {
 public:
  // Create an empty search request.
  ServiceSearchRequest();
  // Parse the parameters given in |params| to initialize this request.
  explicit ServiceSearchRequest(const common::ByteBuffer& params);

  // Request overrides
  bool valid() const override;
  common::ByteBufferPtr GetPDU(TransactionId tid) const override;

  // A service search pattern matches if every UUID in the pattern is contained
  // within one of the services' attribute values.  They don't need to be in any
  // specific attribute or in any particular order, and extraneous UUIDs are
  // allowed to exist in the attribute value.
  // See v5.0, Volume 3, Part B, Sec 2.5.2
  void set_search_pattern(std::unordered_set<common::UUID> pattern) {
    service_search_pattern_ = pattern;
  }
  const std::unordered_set<common::UUID>& service_search_pattern() const {
    return service_search_pattern_;
  }

  // The maximum count of records that should be included in any
  // response.
  void set_max_service_record_count(uint16_t count) {
    max_service_record_count_ = count;
  }
  uint16_t max_service_record_count() const {
    return max_service_record_count_;
  }

 private:
  std::unordered_set<common::UUID> service_search_pattern_;
  uint16_t max_service_record_count_;
};

// Generated by the SDP server in response to a ServiceSearchRequest.
// See v5.0, Volume 3, Part B, Sec 4.5.2
class ServiceSearchResponse : public Response {
 public:
  ServiceSearchResponse();

  // Response overrides
  bool complete() const override;
  const common::BufferView ContinuationState() const override;
  Status Parse(const common::ByteBuffer& buf) override;
  common::MutableByteBufferPtr GetPDU(
      uint16_t max, TransactionId tid,
      const common::ByteBuffer& cont_state) const override;

  // The ServiceRecordHandleList contains as list of service record handles.
  // This should be set to the list of handles that match the request.
  // Limiting the response to the maximum requested is handled by
  // GetParameters();
  void set_service_record_handle_list(std::vector<ServiceHandle> handles) {
    service_record_handle_list_ = handles;
    total_service_record_count_ = handles.size();
  }
  std::vector<ServiceHandle> service_record_handle_list() const {
    return service_record_handle_list_;
  }

 private:
  // The list of service record handles.
  std::vector<ServiceHandle> service_record_handle_list_;
  // The total number of service records in the full response.
  uint16_t total_service_record_count_;

  common::ByteBufferPtr continuation_state_;
};

// Represents a range of attributes, inclusive of |start| and |end|.
struct AttributeRange {
  AttributeRange(AttributeId start, AttributeId end) : start(start), end(end) {
    ZX_DEBUG_ASSERT(start <= end);
  }

  AttributeId start;
  AttributeId end;
};

// Used to retrieve a set of attributes from a specific service record.
// See v5.0, Volume 3, Part B, Sec 4.6.1
class ServiceAttributeRequest : public Request {
 public:
  // Create an empty search request.
  ServiceAttributeRequest();
  // Parse the parameters in |params| to initialize this request.
  // valid() will be false if |params| don't represent valid a valid request.
  explicit ServiceAttributeRequest(const common::ByteBuffer& params);

  // Request overrides
  bool valid() const override;
  common::ByteBufferPtr GetPDU(TransactionId tid) const override;

  void set_service_record_handle(ServiceHandle handle) {
    service_record_handle_ = handle;
  }
  ServiceHandle service_record_handle() const { return service_record_handle_; }

  // Set the maximum size allowed in the response in the Attribute list
  // Not allowed to be lower than kMinMaximumAttributeByteCount (7)
  void set_max_attribute_byte_count(uint16_t count) {
    ZX_DEBUG_ASSERT(count >= kMinMaximumAttributeByteCount);
    max_attribute_byte_count_ = count;
  }
  uint16_t max_attribute_byte_count() const {
    return max_attribute_byte_count_;
  }

  // Adds a single attribute to the requested IDs. Used to ensure a specific
  // attribute is requested.
  // Automatically merges attribute ranges that are contiguous to save bytes in
  // the request.
  void AddAttribute(AttributeId id);

  // Adds a range of attributes to the requested IDs.
  // Like AddAttribute(), attribute ranges that are contiguous are merged to
  // save bytes in the resulting request.
  void AddAttributeRange(AttributeId start, AttributeId end);

  const std::list<AttributeRange>& attribute_ranges() {
    return attribute_ranges_;
  }

 private:
  // The service record handle for which attributes should be retrieved.
  // Should be obtained by using a ServiceSearch transaction.
  ServiceHandle service_record_handle_;

  // Maximum number of bytes of attribute data to be returned in the response.
  // If the attributes don't fit, the server decides how to segment them.
  // Clients should use continuation state to request more data.
  uint16_t max_attribute_byte_count_;

  // The attribute(s) to retrieve.
  // This is a list of ranges, inclusive of the ends.
  // They are non-overlapping and sorted by the start id of each range.
  std::list<AttributeRange> attribute_ranges_;
};

// Generated upon receiving a ServiceAttributeRequest.
// See v5.0, Volume 3, Part B, Sec 4.6.2
class ServiceAttributeResponse : public Response {
 public:
  ServiceAttributeResponse();

  // Response overrides
  const common::BufferView ContinuationState() const override;
  bool complete() const override;
  Status Parse(const common::ByteBuffer& buf) override;
  common::MutableByteBufferPtr GetPDU(
      uint16_t max, TransactionId tid,
      const common::ByteBuffer& cont_state) const override;

  void set_attribute(AttributeId id, DataElement value) {
    attributes_.emplace(id, std::move(value));
  }
  const std::map<AttributeId, DataElement>& attributes() const {
    return attributes_;
  }

 private:
  // The list of attributes that matched the search and their values.
  // This is sorted (it is in ascending order in the response).
  std::map<AttributeId, DataElement> attributes_;

  // Attribute List(s) can be truncated due to:
  //  - Response too long for MTU
  //  - MaxAttributeListByteCount is set too low
  //  - Because the server wants to
  //
  // This contains the partial attribute list response if there is continuation
  // state.
  common::MutableByteBufferPtr partial_response_;

  common::MutableByteBufferPtr continuation_state_;
};

// Combines the capabilities of ServiceSearchRequest and ServiceAttributeRequest
// Note that the record handle is not included in the response by default, and
// myst be requested if needed.
// See v5.0, Volume 3, Part B,Sec 4.7.1
class ServiceSearchAttributeRequest : public Request {
 public:
  // Create an empty service search attribute request.
  ServiceSearchAttributeRequest();
  // Parse the parameters in |params| to initialize this request.
  explicit ServiceSearchAttributeRequest(const common::ByteBuffer& params);

  // Request overrides
  bool valid() const override;
  common::ByteBufferPtr GetPDU(TransactionId tid) const override;

  // A service search pattern matches if every UUID in the pattern is contained
  // within one of the services' attribute values.  They don't need to be in any
  // specific attribute or in any particular order, and extraneous UUIDs are
  // allowed to exist in the attribute value.
  // See v5.0, Volume 3, Part B, Sec 2.5.2.
  void set_search_pattern(std::unordered_set<common::UUID> pattern) {
    service_search_pattern_ = pattern;
  }
  const std::unordered_set<common::UUID>& service_search_pattern() const {
    return service_search_pattern_;
  }

  // Set the maximum size allowed in the response in the Attribute list
  // Not allowed to be lower than kMinMaximumAttributeByteCount (7)
  void set_max_attribute_byte_count(uint16_t count) {
    ZX_DEBUG_ASSERT(count >= kMinMaximumAttributeByteCount);
    max_attribute_byte_count_ = count;
  }
  uint16_t max_attribute_byte_count() const {
    return max_attribute_byte_count_;
  }

  // Adds a single attribute to the requested IDs
  void AddAttribute(AttributeId id);

  // Adds a range of attributes to the requested IDs.
  void AddAttributeRange(AttributeId start, AttributeId end);

  const std::list<AttributeRange>& attribute_ranges() {
    return attribute_ranges_;
  }

 private:
  // The service search pattern to match services.
  std::unordered_set<common::UUID> service_search_pattern_;

  // Maximum number of bytes of attribute data to be returned in the response.
  // If the attributes don't fit, the server decides how to segment them.
  // Clients should use continuation state to request more data.
  uint16_t max_attribute_byte_count_;

  // The attribute(s) to retrieve.
  // This is a list of ranges, inclusive of the ends.
  // They are non-overlapping and sorted by the first attribute id.
  std::list<AttributeRange> attribute_ranges_;
};

// Generated in response to a ServiceSearchAttributeRequest
// See v5.0, Volume 3, Part B,Sec 4.7.2
class ServiceSearchAttributeResponse : public Response {
 public:
  ServiceSearchAttributeResponse();

  // Response overrides
  const common::BufferView ContinuationState() const override;
  bool complete() const override;
  Status Parse(const common::ByteBuffer& buf) override;
  common::MutableByteBufferPtr GetPDU(
      uint16_t max, TransactionId tid,
      const common::ByteBuffer& cont_state) const override;

  // Set an attribute to be included in the response.
  // |idx| is used to group attributes and does not need to be contiguous for
  // convenience (i.e. a service's handle), although parsed responses will
  // be numbered starting from 0.
  void SetAttribute(uint32_t idx, AttributeId id, DataElement value);

  // The number of attribute lists in this response.
  size_t num_attribute_lists() const { return attribute_lists_.size(); }

  // Retrieve attributes in response from a specific index.
  // Attribute lists are numbered starting from 0 when parsed.
  const std::map<AttributeId, DataElement>& attributes(uint32_t idx) const {
    return attribute_lists_.at(idx);
  }

 private:
  // The list of lists that is to be returned / was returned in the response.
  // They are in ascending order of index, which has no relation to the
  // service IDs (they may not be included).
  std::map<uint32_t, std::map<AttributeId, DataElement>> attribute_lists_;

  // The Attribute Lists can be truncated due to:
  //  - Response too long for MTU
  //  - MaxAttributeListByteCount is set too low
  //  - Because the server wants to
  //
  // This contains the partial attribute list response if there is continuation
  // state.
  common::MutableByteBufferPtr partial_response_;

  common::MutableByteBufferPtr continuation_state_;
};

}  // namespace sdp
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_SDP_PDU_H_
