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
  struct Range {
    zx_gpaddr_t addr;
    size_t size;

    bool operator<(const Range& r) const { return addr + size <= r.addr; }

    bool contains(const Range& r) const { return !(r < *this) && !(*this < r); }
  };
  using RangeSet = std::set<Range>;

  bool AddRange(zx_gpaddr_t addr, size_t size) {
    if (size == 0) {
      return false;
    }
    return ranges.emplace(Range{addr,size}).second;
  }

  const RangeSet::const_iterator begin() const { return ranges.begin(); }
  const RangeSet::const_iterator end() const { return ranges.end(); }

  // Generates, by calling the provided functor, all Range's that are in the
  // provided range, that do not overlap with any internal ranges. This means
  // the generated set is precisely the inverse of our contained ranges, unioned
  // with the provided range.
  template<typename F>
  void YieldInverseRange(zx_gpaddr_t base, size_t size, F yield) const {
    zx_gpaddr_t prev = base;
    for (const auto& range: ranges) {
      zx_gpaddr_t next_top = std::min(range.addr, base + size);
      if (next_top > prev) {
        yield(Range{.addr = prev, .size = next_top - prev});
      }
      prev = range.addr + range.size;
    }
    zx_gpaddr_t next_top = std::min(prev, base + size);
    if (next_top > prev) {
      yield(Range{.addr = prev, .size = next_top - prev});
    }
  }

 private:
  RangeSet ranges;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_DEV_MEM_H_
