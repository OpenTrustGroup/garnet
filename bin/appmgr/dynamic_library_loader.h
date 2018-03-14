// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_DYNAMIC_LIBRARY_LOADER_H_
#define GARNET_BIN_APPMGR_DYNAMIC_LIBRARY_LOADER_H_

#include <zircon/types.h>
#include <zx/channel.h>

#include "lib/fxl/files/unique_fd.h"

namespace app {
namespace DynamicLibraryLoader {

zx_status_t Start(fxl::UniqueFD fd, zx::channel* result);

}  // namespace DynamicLibraryLoader
}  // namespace app

#endif  // GARNET_BIN_APPMGR_DYNAMIC_LIBRARY_LOADER_H_
