// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/resource_map.h"

namespace scenic {
namespace gfx {

ResourceMap::ResourceMap(ErrorReporter* error_reporter)
    : error_reporter_(error_reporter) {}

ResourceMap::~ResourceMap() {}

void ResourceMap::Clear() {
  resources_.clear();
}

bool ResourceMap::AddResource(scenic::ResourceId id, ResourcePtr resource) {
  FXL_DCHECK(resource);

  auto result = resources_.insert(std::make_pair(id, std::move(resource)));
  if (!result.second) {
    error_reporter_->ERROR()
        << "scenic::gfx::ResourceMap::AddResource(): resource with ID " << id
        << " already exists.";
    return false;
  }
  return true;
}

bool ResourceMap::RemoveResource(scenic::ResourceId id) {
  size_t erased_count = resources_.erase(id);
  if (erased_count == 0) {
    error_reporter_->ERROR()
        << "scenic::gfx::ResourceMap::RemoveResource(): no resource with ID "
        << id;
    return false;
  }
  return true;
}

}  // namespace gfx
}  // namespace scenic
