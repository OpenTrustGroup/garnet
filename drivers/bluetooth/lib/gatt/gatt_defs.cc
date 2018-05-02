// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gatt_defs.h"

namespace btlib {
namespace gatt {

ServiceData::ServiceData(att::Handle start,
                         att::Handle end,
                         const common::UUID& type)
    : range_start(start), range_end(end), type(type) {}

CharacteristicData::CharacteristicData(Properties props,
                                       att::Handle handle,
                                       att::Handle value_handle,
                                       const common::UUID& type)
    : properties(props),
      handle(handle),
      value_handle(value_handle),
      type(type) {}

DescriptorData::DescriptorData(att::Handle handle, const common::UUID& type)
    : handle(handle), type(type) {}

}  // namespace gatt
}  // namespace btlib
