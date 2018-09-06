// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_FILES_FILE_DESCRIPTOR_H_
#define LIB_FXL_FILES_FILE_DESCRIPTOR_H_

#include <stdint.h>
#include <unistd.h>

#include "lib/fxl/fxl_export.h"

namespace fxl {

FXL_EXPORT bool WriteFileDescriptor(int fd, const char* data, ssize_t size);
FXL_EXPORT ssize_t ReadFileDescriptor(int fd, char* data, ssize_t max_size);

}  // namespace fxl

#endif  // LIB_FXL_FILES_FILE_DESCRIPTOR_H_
