// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/tests/util/container_test_util.h"

namespace f1dl {

size_t CopyableType::num_instances_ = 0;
size_t MoveOnlyType::num_instances_ = 0;

CopyableType::CopyableType() : copied_(false), ptr_(this) {
  num_instances_++;
}

CopyableType::CopyableType(const CopyableType& other)
    : copied_(true), ptr_(other.ptr()) {
  num_instances_++;
}

CopyableType& CopyableType::operator=(const CopyableType& other) {
  copied_ = true;
  ptr_ = other.ptr();
  return *this;
}

CopyableType::~CopyableType() {
  num_instances_--;
}

MoveOnlyType::MoveOnlyType() : moved_(false), ptr_(this) {
  num_instances_++;
}

MoveOnlyType::MoveOnlyType(MoveOnlyType&& other)
    : moved_(true), ptr_(other.ptr()) {
  num_instances_++;
}

MoveOnlyType& MoveOnlyType::operator=(MoveOnlyType&& other) {
  moved_ = true;
  ptr_ = other.ptr();
  return *this;
}

MoveOnlyType::~MoveOnlyType() {
  num_instances_--;
}

}  // namespace f1dl
