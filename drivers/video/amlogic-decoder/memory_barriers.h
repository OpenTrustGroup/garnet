// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_MEMORY_BARRIERS_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_MEMORY_BARRIERS_H_

// This barrier should be used after a cache flush of memory before a MMIO is
// made to access it from the hardware.
inline void BarrierAfterFlush() {
#if defined(__aarch64__)
  // According to the ARMv8 ARM K11.5.4 it's better to use DSB instead of DMB
  // for ordering with respect to MMIO (DMB is ok if all agents are just
  // observing memory). The system shareability domain is used because that's
  // the only domain the video decoder is guaranteed to be in. SY is used
  // instead of LD or ST because section B2.3.5 says that the barrier needs both
  // read and write access types to be effective with regards to cache
  // operations.
  asm __volatile__("dsb sy");
#else
#error need definition for this platform
#endif
}

// This barrier should be used after the hardware has signaled that memory has
// data but before the cache invalidate. See ARMv8 ARM K11.5.1 for the reason
// a barrier is necessary.
inline void BarrierBeforeInvalidate() {
#if defined(__aarch64__)
  // According to the ARMv8 ARM K11.5.4 it's better to use DSB instead of DMB
  // for ordering with respect to MMIO (DMB is ok if all agents are just
  // observing memory). The system shareability domain is used because that's
  // the only domain the video decoder is guaranteed to be in. SY is used
  // instead of LD or ST because section B2.3.5 says that the barrier needs both
  // read and write access types to be effective with regards to cache
  // operations.
  asm __volatile__("dsb sy");
#else
#error need definition for this platform
#endif
}

// This barrier should be used after hardware has signaled it's done with a
// buffer but before releasing it. It's probably often unnecessary to use this
// barrier because there is another implicit dependency relationship.
inline void BarrierBeforeRelease() {
#if defined(__aarch64__)
  // According to the ARMv8 ARM K11.5.4 it's better to use DSB instead of DMB
  // for ordering with respect to MMIO (DMB is ok if all agents are just
  // observing memory). The system shareability domain is used because that's
  // the only domain the video decoder is guaranteed to be in. SY is used
  // instead of ST because we're not sure about the next operation on the
  // buffer, and LD isn't used because the caller may have determined that the
  // buffer can be released in several ways.
  asm __volatile__("dsb sy");
#else
#error need definition for this platform
#endif
}

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_MEMORY_BARRIERS_H_
