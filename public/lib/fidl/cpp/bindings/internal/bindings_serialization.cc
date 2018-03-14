// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/internal/bindings_serialization.h"

#include <zircon/assert.h>

namespace f1dl {
namespace internal {

namespace {

const size_t kAlignment = 8;

template <typename T>
T AlignImpl(T t) {
  return t + (kAlignment - (t % kAlignment)) % kAlignment;
}

}  // namespace

size_t Align(size_t size) {
  return AlignImpl(size);
}

char* AlignPointer(char* ptr) {
  return reinterpret_cast<char*>(AlignImpl(reinterpret_cast<uintptr_t>(ptr)));
}

bool IsAligned(const void* ptr) {
  return !(reinterpret_cast<uintptr_t>(ptr) % kAlignment);
}

void EncodePointer(const void* ptr, uint64_t* offset) {
  if (!ptr) {
    *offset = 0;
    return;
  }

  const char* p_obj = reinterpret_cast<const char*>(ptr);
  const char* p_slot = reinterpret_cast<const char*>(offset);
  ZX_DEBUG_ASSERT(p_obj > p_slot);

  *offset = static_cast<uint64_t>(p_obj - p_slot);
}

const void* DecodePointerRaw(const uint64_t* offset) {
  if (!*offset)
    return nullptr;
  return reinterpret_cast<const char*>(offset) + *offset;
}

void EncodeHandle(WrappedHandle* handle, std::vector<zx_handle_t>* handles) {
  if (handle->value != ZX_HANDLE_INVALID) {
    handles->push_back(handle->value);
    handle->value = static_cast<zx_handle_t>(handles->size() - 1);
  } else {
    handle->value = kEncodedInvalidHandleValue;
  }
}

void EncodeHandle(Interface_Data* data, std::vector<zx_handle_t>* handles) {
  EncodeHandle(&data->handle, handles);
}

void DecodeHandle(WrappedHandle* handle, std::vector<zx_handle_t>* handles) {
  if (handle->value == kEncodedInvalidHandleValue) {
    handle->value = ZX_HANDLE_INVALID;
    return;
  }
  ZX_DEBUG_ASSERT(static_cast<size_t>(handle->value) < handles->size());
  // Just leave holes in the vector so we don't screw up other indices.
  handle->value = FetchAndReset(&handles->at(handle->value));
}

void DecodeHandle(Interface_Data* data, std::vector<zx_handle_t>* handles) {
  DecodeHandle(&data->handle, handles);
}

}  // namespace internal
}  // namespace f1dl
