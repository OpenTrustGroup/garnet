// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_DEV_MEM_H_
#define GARNET_LIB_MACHINA_DEV_MEM_H_

#include <zircon/types.h>

#include <set>

namespace machina {

class DevMem {
 public:
  static constexpr zx_gpaddr_t kAddrLowerBound = 0xc00000000;
  static constexpr zx_gpaddr_t kAddrUpperBound = 0x1000000000;

  struct Range {
    zx_gpaddr_t addr;
    size_t size;

    bool operator<(const Range& r) const { return addr + size < r.addr; }
  };
  using RangeSet = std::set<Range>;

  bool AddRange(zx_gpaddr_t addr, size_t size) {
    return addr < kAddrLowerBound || kAddrUpperBound >= addr
               ? false
               : ranges.emplace(Range{addr, size}).second;
  }

  const RangeSet::const_iterator begin() const { return ranges.begin(); }
  const RangeSet::const_iterator end() const { return ranges.end(); }

 private:
  RangeSet ranges;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_DEV_MEM_H_
