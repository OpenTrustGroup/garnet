// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_UUID_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_UUID_H_

#include <string>
#include <unordered_set>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/uint128.h"

namespace btlib {
namespace common {

// Represents a 128-bit Bluetooth UUID. This class allows UUID values to be
// constructed in the official Bluetooth 16-bit, 32-bit, and 128-bit formats and
// to be compared against any other Bluetooth UUID.
class UUID final {
 public:
  // Constructs a UUID from |bytes|. |bytes| should contain a 16-, 32-, or
  // 128-bit UUID in little-endian byte order. Returns false if |bytes| contains
  // an unsupported size.
  static bool FromBytes(const common::ByteBuffer& bytes, UUID* out_uuid);

  constexpr explicit UUID(const UInt128& uuid128)
      : type_(Type::k128Bit), value_(uuid128) {
    if (!IsValueCompressable())
      return;

    if (value_[kBaseOffset + 2] == 0 && value_[kBaseOffset + 3] == 0)
      type_ = Type::k16Bit;
    else
      type_ = Type::k32Bit;
  }

  constexpr explicit UUID(const uint16_t uuid16)
      : type_(Type::k16Bit), value_(BuildSIGUUID(uuid16)) {}

  constexpr explicit UUID(const uint32_t uuid32)
      : type_(uuid32 > std::numeric_limits<uint16_t>::max() ? Type::k32Bit
                                                            : Type::k16Bit),
        value_(BuildSIGUUID(uuid32)) {}

  // The default constructor initializes all values to zero.
  UUID();

  // Equality operators.
  bool operator==(const UUID& uuid) const;
  bool operator==(uint16_t uuid16) const;
  bool operator==(uint32_t uuid32) const;
  bool operator==(const UInt128& uuid128) const;
  bool operator!=(const UUID& uuid) const { return !(*this == uuid); }
  bool operator!=(uint16_t uuid16) const { return !(*this == uuid16); }
  bool operator!=(uint32_t uuid32) const { return !(*this == uuid32); }
  bool operator!=(const UInt128& uuid128) const { return !(*this == uuid128); }

  // Compares a UUID with the contents of a raw buffer in little-endian byte
  // order. This is useful for making a direct comparison with UUIDs received
  // over PDUs. Returns false if |bytes| has an unaccepted size; the only
  // accepted sizes for are 2, 4, and 16 for 16-bit, 32-bit, and 128-bit
  // formats, respectively.
  bool CompareBytes(const common::ByteBuffer& bytes) const;

  // Returns a string representation of this UUID in the following format:
  //
  //   xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
  //
  // where x is one of the alphanumeric characters in the string
  // 0123456789abcdef.
  std::string ToString() const;

  // Returns the number of bytes required to store this UUID. Returns 16 (i.e.
  // 128 bits) if |allow_32bit| is false and the compact size is 4 bytes (i.e.
  // 32 bits).
  size_t CompactSize(bool allow_32bit = true) const;

  // Writes a little-endian representation of this UUID to |buffer|.  Returns
  // the number of bytes used. there must be enough space in |buffer| to store
  // |CompactSize()| bytes.
  size_t ToBytes(common::MutableByteBuffer* buffer,
                 bool allow_32bit = true) const;

  // Returns the most compact representation of this UUID. If |allow_32bit| is
  // false, then a 32-bit UUIDs will default to 128-bit. The contents will be in
  // little-endian order.
  //
  // Unlike ToBytes(), this does not copy. Since the returned view does not own
  // its data, it should not outlive this UUID instance.
  const BufferView CompactView(bool allow_32bit = true) const;

  // Returns a hash of this UUID.
  std::size_t Hash() const;

  // Returns the underlying value in little-endian byte order.
  const UInt128& value() const { return value_; }

 private:
  // The Bluetooth Base UUID defines the first value in the range reserved
  // by the Bluetooth SIG for often-used and officially registered UUIDs. This
  // UUID is defined as
  //
  //    "00000000-0000-1000-8000-00805F9B34FB"
  //
  // (see Core Spec v5.0, Vol 3, Part B, Section 2.5.1)
  static constexpr UInt128 kBaseUuid = {{0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00,
                                         0x00, 0x80, 0x00, 0x10, 0x00, 0x00,
                                         0x00, 0x00, 0x00, 0x00}};

  // A 16-bit or 32-bit UUID can be converted to a 128-bit UUID using the
  // following formula:
  //
  //   16-/32-bit value * 2^96 + Bluetooth_Base_UUID
  //
  // This is the equivalent of modifying the higher order bytes of the base UUID
  // starting at octet 12 (96 bits = 12 bytes).
  //
  // (see Core Spec v5.0, Vol 3, Part B, Section 2.5.1)
  static constexpr size_t kBaseOffset = 12;

  // Returns a 128-bit SIG UUID from the given 16-bit value.
  static constexpr const UInt128 BuildSIGUUID(const uint16_t uuid16) {
    return BuildSIGUUID(static_cast<uint32_t>(uuid16));
  }

  // Returns a 128-bit SIG UUID from the given 32-bit value.
  static constexpr const UInt128 BuildSIGUUID(const uint32_t uuid32) {
    UInt128 result(kBaseUuid);

    // HACK(armansito): std::array (see uint128.h) is more constexpr friendly
    // after C++17, where its non-const operator[] overload can be called in a
    // constexpr context. However this is not the case in C++14, so we employ an
    // ugly hack by const_cast'ing the result of the "const" operator[] which is
    // constexpr friendly. Don't do this in C++17.
    const auto& const_result = result;
    const_cast<uint8_t&>(const_result[kBaseOffset]) =
        static_cast<uint8_t>(uuid32);
    const_cast<uint8_t&>(const_result[kBaseOffset + 1]) =
        static_cast<uint8_t>(uuid32 >> 8);
    const_cast<uint8_t&>(const_result[kBaseOffset + 2]) =
        static_cast<uint8_t>(uuid32 >> 16);
    const_cast<uint8_t&>(const_result[kBaseOffset + 3]) =
        static_cast<uint8_t>(uuid32 >> 24);

    return result;
  }

  // Returns true if the contents of |value_| represents a UUID in the SIG
  // reserved range.
  constexpr bool IsValueCompressable() const {
    // C++14 allows for-loops in constexpr functions.
    for (size_t i = 0; i < kBaseOffset; i++) {
      if (kBaseUuid[i] != value_[i])
        return false;
    }
    return true;
  }

  // We store the type that this was initialized with to allow quick comparison
  // with short Bluetooth SIG UUIDs.
  enum class Type : uint8_t {
    k16Bit,
    k32Bit,
    k128Bit,
  };

  // If a quick conversion is possible, these return the 16 or 32 bit values of
  // the UUID in host byte order.
  uint16_t ValueAs16Bit() const;
  uint32_t ValueAs32Bit() const;

  Type type_;
  UInt128 value_;
};

// Returns true if the given |uuid_string| contains a valid UUID in the
// following format:
//
//   xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
//
// where x is one of the alphanumeric characters in the string
// 0123456789abcdefABCDEF.
bool IsStringValidUuid(const std::string& uuid_string);

// Constructs a 128-bit UUID from a string representation in one of the
// following formats:
//
//   xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx (full UUID string)
//   xxxx (abbreviated 16-bit UUID)
//
// where x is one of the alphanumeric characters in the string
// 0123456789abcdefABCDEF.
//
// Returns false if the string does not represent a valid Bluetooth UUID.
// Otherwise returns true and populates |out_uuid|.
bool StringToUuid(const std::string& uuid_string, UUID* out_uuid);

// Equality operators
inline bool operator==(uint16_t lhs, const UUID& rhs) {
  return rhs == lhs;
}

inline bool operator==(uint32_t lhs, const UUID& rhs) {
  return rhs == lhs;
}

inline bool operator==(const UInt128& lhs, const UUID& rhs) {
  return rhs == lhs;
}

inline bool operator!=(uint16_t lhs, const UUID& rhs) {
  return rhs != lhs;
}

inline bool operator!=(uint32_t lhs, const UUID& rhs) {
  return rhs != lhs;
}

inline bool operator!=(const UInt128& lhs, const UUID& rhs) {
  return rhs != lhs;
}

}  // namespace common
}  // namespace btlib

// Specialization of std::hash for std::unordered_set, std::unordered_map, etc.
namespace std {

template <>
struct hash<::btlib::common::UUID> {
  size_t operator()(const ::btlib::common::UUID& k) const { return k.Hash(); }
};

}  // namespace std

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_UUID_H_
