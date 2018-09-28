// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_SDP_DATA_ELEMENT_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_SDP_DATA_ELEMENT_H_

#include <vector>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/optional.h"
#include "garnet/drivers/bluetooth/lib/common/uuid.h"

namespace btlib {
namespace sdp {

// See v5.0, Vol 3, Part B, Sec 3.1.
// Each Data element has a header and a data field.
// The header field contains a type descriptor and a size descriptor.
// The header field is of variable length, which is determined by the size
// descriptor.
//
// DataElements all start as the Null type, and then can be set to any type.
// Examples:
// DataElement elem;  // A null type
// uint32_t seymour = 0xFEED;
// elem.Set(seymour);
// ZX_DEBUG_ASSERT(elem.type() == DataElement::Type::kUnsignedInt);
// ZX_DEBUG_ASSERT(elem.Get<uint32_t>());
// ZX_DEBUG_ASSERT(*elem.Get<uint32_t>() == seymour);
//
// std::vector<DataElement> service_class_ids;
// DataElement uuid;
// uuid.Set(UUID(sdp::ServiceClass::kAudioSource));
// service_class_ids.emplace_back(std::move(uuid));
// elem.Set(service_class_ids);
// ZX_DEBUG_ASSERT(e.type() == DataElement::Type::kSeqeuence);
// ZX_DEBUG_ASSERT(!e.Get<uint32_t>());
class DataElement {
 public:
  // Type Descriptors. Only the top 5 bits are used, see kTypeMask
  // v5.0, Vol 3, Part B, Sec 3.2.
  enum class Type : uint8_t {
    kNull = (0 << 3),
    kUnsignedInt = (1 << 3),
    kSignedInt = (2 << 3),
    kUuid = (3 << 3),
    kString = (4 << 3),
    kBoolean = (5 << 3),
    kSequence = (6 << 3),
    kAlternative = (7 << 3),
    kUrl = (8 << 3),
  };
  constexpr static uint8_t kTypeMask = 0xF8;

  // Size Descriptor describing the size of the data following.
  // Only three bits are used.
  // For 0-4, the size is 2^(value) except in the case of kNull, in which
  // case the size is 0.
  // otherwise, the size is described in 2^(5-value) extra bytes following.
  // v45.0, Vol 3, Part B, Sec 3.3
  enum class Size : uint8_t {
    kOneByte = 0,
    kTwoBytes = 1,
    kFourBytes = 2,
    kEightBytes = 3,
    kSixteenBytes = 4,
    kNextOne = 5,
    kNextTwo = 6,
    kNextFour = 7,
  };
  constexpr static uint8_t kSizeMask = 0x07;

  // Constructs a Null data element.
  DataElement();
  ~DataElement() = default;

  // Default move constructor and move-assigment
  DataElement(DataElement&&) = default;
  DataElement& operator=(DataElement&&) = default;

  // Convenience constructor to create a DataElement from a basic type.
  template <typename T>
  explicit DataElement(T value) {
    Set<T>(std::move(value));
  };

  // Make a deep copy of this element.
  DataElement Clone() const { return DataElement(*this); }

  // Reads a DataElement from |buffer|, replacing any data that was in |elem|.
  // Returns the amount of space occupied on |buffer| by the data element, or
  // zero if no element could be read.
  static size_t Read(DataElement* elem, const common::ByteBuffer& buffer);

  // The type of this element.
  Type type() const { return type_; }

  // The size of this element.
  Size size() const { return size_; }

  // Sets the value of this element to |value|.
  // Defined specializations:
  // typename                - type()
  // std::nullptr_t          - kNull
  // uint8_t, .., uint64_t   - kUnsignedInt
  // int8_t .. int64_t       - kSignedInt
  // const common::UUID&     - kUuid
  // const std::string&      - kString
  // bool                    - kBoolean
  // std::vector<DataElemnt> - kSequence
  // (not available)         - kUrl (not used in any known profiles)
  template <typename T>
  void Set(T value);

  // Sets this element's value to an alternative of the items in |items|
  void SetAlternative(std::vector<DataElement> items);

  // Get the value of this element.
  // Has the same defined specializations as Set().
  // Returns an optional without a value if the wrong type is stored.
  template <typename T>
  common::Optional<T> Get() const;

  // Get a pointer to an element in a DataElement Sequence.
  // Returns nullptr if type() is not kSequence or the index is invalid.
  // Only valid as long as the containing sequence is valid.
  const DataElement* At(size_t idx) const;

  // Calculates the number of bytes that this DataElement will use if it's
  // written using Write().
  size_t WriteSize() const;

  // Writes this DataElement to |buffer|.
  // Returns the number of bytes used for writing this element.
  size_t Write(common::MutableByteBuffer* buffer) const;

  // Debug representation of this element (including it's type and size) in a
  // string, i.e. UnsignedInt:4(15) or Sequence { UUID(1567), UUID(2502) }
  std::string ToString() const;

 private:
  // Copy constructor for Clone(), no assignment operator.
  DataElement(const DataElement&);
  DataElement& operator=(const DataElement&) = delete;
  // Sets the size type based on a variable size (Next one, two, or four)
  void SetVariableSize(size_t length);

  Type type_;
  Size size_;

  // Various types for the stored value.  These are only valid if the type_ is
  // set correctly.
  int64_t int_value_;
  uint64_t uint_value_;
  common::UUID uuid_;
  std::string string_;
  std::vector<DataElement> aggregate_;
};

}  // namespace sdp
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_SDP_DATA_ELEMENT_H_
