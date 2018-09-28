// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/lib/debug_ipc/records.h"

namespace zxdb {

class Err;
class OutputBuffer;
class Register;

// Processed is true if the register belongs to the X64 family, and the output
// was processed by this function.
// |err| should be checked for errors if the result was processed.
bool FormatCategoryX64(debug_ipc::RegisterCategory::Type category,
                       const std::vector<Register>& registers,
                       OutputBuffer* out, Err* err);

}  // namespace zxdb
