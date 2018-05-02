// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FSL_VMO_FILE_H_
#define LIB_FSL_VMO_FILE_H_

#include <lib/zx/vmo.h>

#include <string>

#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/fxl_export.h"

namespace fsl {

// Make a new shared buffer with the contents of a file.
FXL_EXPORT bool VmoFromFd(fxl::UniqueFD fd, SizedVmo* handle_ptr);

// Make a new shared buffer with the contents of a file.
FXL_EXPORT bool VmoFromFilename(const std::string& filename,
                                SizedVmo* handle_ptr);

// Make a new shared buffer with the contents of a file.
FXL_EXPORT bool VmoFromFilenameAt(int dirfd,
                                  const std::string& filename,
                                  SizedVmo* handle_ptr);

}  // namespace fsl

#endif  // LIB_FSL_VMO_FILE_H_
