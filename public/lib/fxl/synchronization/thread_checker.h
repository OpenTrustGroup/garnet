// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A class for checking that the current thread is/isn't the same as an initial
// thread.

#ifndef LIB_FXL_SYNCHRONIZATION_THREAD_CHECKER_H_
#define LIB_FXL_SYNCHRONIZATION_THREAD_CHECKER_H_

#include "lib/fxl/build_config.h"

#include <pthread.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace fxl {

// A simple class that records the identity of the thread that it was created
// on, and at later points can tell if the current thread is the same as its
// creation thread. This class is thread-safe.
//
// Note: Unlike Chromium's |base::ThreadChecker|, this is *not* Debug-only (so
// #ifdef it out if you want something Debug-only). (Rationale: Having a
// |CalledOnValidThread()| that lies in Release builds seems bad. Moreover,
// there's a small space cost to having even an empty class. )
class ThreadChecker final {
 public:
  ThreadChecker() : self_(pthread_self()) {}
  ~ThreadChecker() {}

  // Returns true if the current thread is the thread this object was created
  // on and false otherwise.
  bool IsCreationThreadCurrent() const {
    return !!pthread_equal(pthread_self(), self_);
  }

 private:
  const pthread_t self_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ThreadChecker);
};

#ifndef NDEBUG
#define FXL_DECLARE_THREAD_CHECKER(c) fxl::ThreadChecker c
#define FXL_DCHECK_CREATION_THREAD_IS_CURRENT(c) \
  FXL_DCHECK((c).IsCreationThreadCurrent())
#else
#define FXL_DECLARE_THREAD_CHECKER(c)
#define FXL_DCHECK_CREATION_THREAD_IS_CURRENT(c) ((void)0)
#endif

}  // namespace fxl

#endif  // LIB_FXL_SYNCHRONIZATION_THREAD_CHECKER_H_
