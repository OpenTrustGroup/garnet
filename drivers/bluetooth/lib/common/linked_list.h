// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_LINKED_LIST_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_LINKED_LIST_H_

#include <memory>

#include <fbl/intrusive_double_list.h>

#include "garnet/drivers/bluetooth/lib/common/intrusive_pointer_traits.h"

namespace btlib {
namespace common {

// TODO(armansito): Use this in more places where it makes sense (see NET-176).

// Convenience template aliases for an fbl intrusive container backed
// LinkedList.
//
//   * Elements need to be dynamically allocated and entries MUST be managed
//     pointers.
//
//   * This is currently implemented as a doubly linked-list. This adds extra
//     storage overhead but makes it suitable for FIFO queues.

template <typename T>
using LinkedList = fbl::DoublyLinkedList<std::unique_ptr<T>>;

template <typename T>
using LinkedListable = fbl::DoublyLinkedListable<std::unique_ptr<T>>;

}  // namespace common
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_LINKED_LIST_H_
