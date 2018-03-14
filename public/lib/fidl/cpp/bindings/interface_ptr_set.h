// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERFACE_PTR_SET_H_
#define LIB_FIDL_CPP_BINDINGS_INTERFACE_PTR_SET_H_

#include <assert.h>

#include <vector>

#include "lib/fidl/cpp/bindings/interface_ptr.h"

namespace f1dl {

// An InterfacePtrSet contains a collection of InterfacePtrs
// that are automatically removed from the collection and destroyed
// when their associated channel experiences a connection error.
// When the set is destroyed all of the channels will be closed.
template <typename Interface>
class InterfacePtrSet {
 public:
  InterfacePtrSet() {}
  ~InterfacePtrSet() { CloseAll(); }

  // |ptr| must be bound to a channel.
  void AddInterfacePtr(InterfacePtr<Interface> ptr) {
    assert(ptr.is_bound());
    ptrs_.emplace_back(std::move(ptr));
    InterfacePtr<Interface>& intrfc_ptr = ptrs_.back();
    Interface* pointer = intrfc_ptr.get();
    // Set the connection error handler for the newly added InterfacePtr to be a
    // function that will erase it from the vector.
    intrfc_ptr.set_error_handler([pointer, this]() {
      // Since InterfacePtr itself is a movable type, the thing that uniquely
      // identifies the InterfacePtr we wish to erase is its Interface*.
      auto it = std::find_if(ptrs_.begin(), ptrs_.end(),
                             [pointer](const InterfacePtr<Interface>& p) {
                               return (p.get() == pointer);
                             });
      assert(it != ptrs_.end());
      ptrs_.erase(it);
    });
  }

  // Applies |function| to each of the InterfacePtrs in the set.
  template <typename FunctionType>
  void ForAllPtrs(FunctionType function) {
    for (const auto& it : ptrs_) {
      if (it)
        function(it.get());
    }
  }

  // Closes the channel associated with each of the InterfacePtrs in
  // this set and clears the set.
  void CloseAll() {
    for (auto& it : ptrs_) {
      if (it)
        it.Unbind();
    }
    ptrs_.clear();
  }

  size_t size() const { return ptrs_.size(); }

 private:
  std::vector<InterfacePtr<Interface>> ptrs_;
};

}  // namespace f1dl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERFACE_PTR_SET_H_
