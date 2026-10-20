// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pthread.h>

namespace machina {

using RwLock = pthread_rwlock_t;

// RAII
class RwLockRead {
 public:
  explicit RwLockRead(RwLock& rwlock) : rwlock_(rwlock) {
    pthread_rwlock_rdlock(&rwlock_);
  }

  ~RwLockRead() { pthread_rwlock_unlock(&rwlock_); }

  RwLockRead(const RwLockRead&) = delete;
  RwLockRead& operator=(const RwLockRead&) = delete;

 private:
  RwLock& rwlock_;
};

class RwLockWrite {
 public:
  explicit RwLockWrite(RwLock& rwlock) : rwlock_(rwlock) {
    pthread_rwlock_wrlock(&rwlock_);
  }

  ~RwLockWrite() { pthread_rwlock_unlock(&rwlock_); }

  RwLockWrite(const RwLockWrite&) = delete;
  RwLockWrite& operator=(const RwLockWrite&) = delete;

 private:
  RwLock& rwlock_;
};

}  // namespace machina
