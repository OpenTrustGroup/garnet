// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <threads.h>

#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddktl/device.h>
#include <fbl/macros.h>
#include <lib/fxl/macros.h>
#include <lib/zx/vmo.h>
#include <garnet/public/lib/guest_allocator/cpp/guest_allocator.h>

class Allocator : public ddk::Device<Allocator, ddk::Ioctlable> {
 public:
  explicit Allocator(zx_device_t* parent, void *fdt);

  static zx_status_t Bind(void* ctx, zx_device_t* parent);

  void DdkRelease() { delete this; }

  zx_status_t GetGuestMemory(const char* compatible, mem_resp_t* out_resp);
  zx_status_t GetGuestMemoryWithPath(const char* path, mem_resp_t* out_resp);
  zx_status_t GetReservedMemory(const char* search_string, mem_resp_t* out_resp);
  zx_status_t GetSystemMemory(mem_resp_t* out_resp);
  zx_status_t GetPropWithPath(const char *path, uint64_t *out_prop);
  zx_status_t GetGuestDtbMemory(zx_handle_t* out_resp);

  zx_status_t DdkIoctl(uint32_t op,
                       const void* in_buf,
                       size_t in_len,
                       void* out_buf,
                       size_t out_len,
                       size_t* out_actual);

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Allocator);
  void *fdt_ __UNUSED;
};
