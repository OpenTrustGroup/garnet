// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_BLOCK_DISPATCHER_H_
#define GARNET_LIB_MACHINA_BLOCK_DISPATCHER_H_

#include <sys/types.h>
#include <vector>

#include <fbl/unique_ptr.h>
#include <zircon/types.h>
#include <zircon/device/vblock.h>
#include <virtio/vblock.h>

namespace machina {

class PhysMem;

zx_status_t DoVblockIoctl(int fd, uint32_t request, void* buf, size_t size,
                          const char* dispatcher_type);

class BlockDispatcher {
public:
  enum class Mode {
    RO,
    RW,
  };
  enum class DataPlane {
    FDIO,
    DIRECTIO,
    FIFO,
  };

  struct DispatcherOptions {
    Mode mode = Mode::RO;
    DataPlane data_plane = DataPlane::FDIO;
    int vmid = INT32_MAX;
  };

  enum class GuidType {
    NONE,
    GPT_PARTITION_GUID,
    GPT_PARTITION_TYPE_GUID,
  };
  struct Guid {
    GuidType type = GuidType::NONE;
    uint8_t bytes[16];

    bool empty() const { return type == GuidType::NONE; }
  };
  enum bio_ioctl_num_ext {
    BIO_IOCTL_CUSTOM_START = 0x100, /* for custom to add new enum */
    BIO_IOCTL_SET_BOOT_REGION = BIO_IOCTL_CUSTOM_START,
    BIO_IOCTL_SET_WRITE_PROTECT,
    BIO_IOCTL_GET_BOOTDEV_TYPE,
    BIO_IOCTL_GET_ACTIVE_BOOT,
  };
  static zx_status_t
  CreateVolatileWrapper(fbl::unique_ptr<BlockDispatcher> dispatcher,
                        fbl::unique_ptr<BlockDispatcher> *out);

  static zx_status_t
  CreateFromPath(const char *path, const DispatcherOptions &options,
                 const PhysMem &phys_mem,
                 fbl::unique_ptr<BlockDispatcher> *dispatcher);

  static zx_status_t
  CreateFromGuid(const Guid &guid, zx_duration_t timeout,
                 const DispatcherOptions &options, const PhysMem &phys_mem,
                 fbl::unique_ptr<BlockDispatcher> *dispatcher);

  static zx_status_t CreateFromFd(int fd, const DispatcherOptions &options,
                                  const PhysMem &phys_mem,
                                  fbl::unique_ptr<BlockDispatcher> *dispatcher);

  BlockDispatcher(size_t size, bool read_only)
      : size_(size), read_only_(read_only) {}
  virtual ~BlockDispatcher() = default;

  virtual bool IsAsyncMode() const { return false; }
  virtual zx_status_t Flush() = 0;
  virtual zx_status_t Read(off_t disk_offset, void *buf, size_t size) = 0;
  virtual zx_status_t Write(off_t disk_offset, const void *buf,
                            size_t size) = 0;
  virtual zx_status_t ioctl(uint32_t request, void *buf, size_t size) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  virtual zx_status_t Submit() = 0;
  virtual void Shutdown() {}

  virtual zx_status_t SubmitBatch(const fifo_in_item* items, size_t count,
                                  const uint64_t* cookies,
                                  const uint32_t* req_ids) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  virtual zx_status_t SubmitAsync(uint64_t addr, size_t size, uint32_t head,
                                  uint64_t cookie) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  virtual zx_status_t WaitForCompletionBatch(fifo_out_item* items,
                                             size_t max_count, size_t* count) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  virtual uint64_t GetCompletionCookie(uint32_t head) const {
    return 0;
  }

  virtual zx_status_t WaitForCompletion(uint32_t *head, uint64_t *cookie) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  virtual void SetBlkConfig(virtio_blk_config_t &config, bool &crypto) {}
  virtual void SetCtxStore(size_t num) {}
  virtual int Vmid() const { return -1; }

  bool read_only() const { return read_only_; }
  size_t size() const { return size_; }
  size_t blk_size() const { return blk_size_; };
  size_t max_seg_nums() const { return max_seg_nums_; };
  size_t max_seg_size() const { return max_seg_size_; }

protected:
  size_t size_;
  bool read_only_;
  size_t blk_size_;
  size_t max_seg_nums_;
  size_t max_seg_size_;
};

} // namespace machina

#endif // GARNET_LIB_MACHINA_BLOCK_DISPATCHER_H_
