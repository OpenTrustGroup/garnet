// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/memory.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo Memory::kTypeInfo = {ResourceType::kMemory, "Memory"};

Memory::Memory(Session* session, ResourceId id,
               const ResourceTypeInfo& type_info)
    : Resource(session, id, type_info) {}

}  // namespace gfx
}  // namespace scenic_impl
