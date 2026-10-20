// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/block_dispatcher.h"

#include <fcntl.h>
#include <limits.h>
#include <string>
#include <string.h>
#include <utility>
#include <unistd.h>
#include <algorithm>

#include <block-client/client.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fdio/watcher.h>
#include <virtio/virtio_ids.h>
#include <virtio/virtio_ring.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/vmid.h>

#include "garnet/lib/machina/phys_mem.h"
#include "garnet/lib/machina/volatile_write_block_dispatcher.h"
#include "garnet/lib/machina/vm_id.h"
#include "lib/fxl/logging.h"

namespace machina {

constexpr char kBlockDirPath[] = "/dev/class/block";
constexpr size_t kDefaultMaxSegSize = 4096;
constexpr uint32_t kSectorSize = 512;
constexpr uint32_t kDefaultGeometry = 128;
constexpr int kMaxIoctlRetries = 100;

zx_status_t BioToVblockIoctl(uint32_t request, int *op) {
  switch (request) {
  case BlockDispatcher::BIO_IOCTL_SET_BOOT_REGION:
    *op = IOCTL_VBLOCK_SET_BOOT_REGION;
    return ZX_OK;
  case BlockDispatcher::BIO_IOCTL_SET_WRITE_PROTECT:
    *op = IOCTL_VBLOCK_SET_WRITE_PROTECT;
    return ZX_OK;
  case BlockDispatcher::BIO_IOCTL_GET_BOOTDEV_TYPE:
    *op = IOCTL_VBLOCK_GET_BOOTDEV_TYPE;
    return ZX_OK;
  case BlockDispatcher::BIO_IOCTL_GET_ACTIVE_BOOT:
    *op = IOCTL_VBLOCK_GET_ACTIVE_BOOT;
    return ZX_OK;
  default:
    return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t DoVblockIoctl(int fd, uint32_t request, void *buf, size_t size,
                          const char *dispatcher_type) {
  int op = 0;

  zx_status_t status = BioToVblockIoctl(request, &op);
  if (status != ZX_OK) {
    return status;
  }

  status = fdio_ioctl(fd, op, buf, size, buf, size);
  if (status < 0) {
    FXL_LOG(ERROR) << "block_dispatcher ioctl " << dispatcher_type
                   << " ioctl failed, ret:" << status
                   << ", request:" << request;
    return status;
  }

  return ZX_OK;
}

class FdioBlockDispatcher : public BlockDispatcher {
public:
  static zx_status_t Create(int fd, size_t size, bool read_only,
                            const PhysMem &phys_mem,
                            fbl::unique_ptr<BlockDispatcher> *out) {
    fbl::AllocChecker ac;
    auto dispatcher = fbl::make_unique_checked<FdioBlockDispatcher>(
        &ac, size, read_only, fd);
    if (!ac.check())
      return ZX_ERR_NO_MEMORY;

    dispatcher->blk_size_ = kSectorSize;
    dispatcher->max_seg_nums_ = 1;
    dispatcher->max_seg_size_ = kDefaultMaxSegSize;

    block_info_t info;
    zx_status_t ret = ioctl_block_get_info(fd, &info);
    if (ret != sizeof(info)) {
      FXL_LOG(INFO) << "block_dispatcher ioctl get info failed, using fd size";
      *out = fbl::move(dispatcher);
      return ZX_OK;
    }

    dispatcher->size_ = info.block_count * info.block_size;
    dispatcher->blk_size_ = info.block_size;
    dispatcher->max_seg_nums_ = info.max_seg_nums;
    dispatcher->max_seg_size_ = info.max_transfer_size;
    FXL_LOG(INFO) << "max seg size: " << dispatcher->max_seg_size_;

    *out = fbl::move(dispatcher);
    return ZX_OK;
  }

  FdioBlockDispatcher(size_t size, bool read_only, int fd)
      : BlockDispatcher(size, read_only), fd_(fd) {}

  ~FdioBlockDispatcher() override {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

  zx_status_t Flush() override {
    fbl::AutoLock lock(&file_mutex_);
    return fsync(fd_) == 0 ? ZX_OK : ZX_ERR_IO;
  }

  zx_status_t Read(off_t disk_offset, void *buf, size_t size) override {
    fbl::AutoLock lock(&file_mutex_);
    off_t off = lseek(fd_, disk_offset, SEEK_SET);
    if (off < 0)
      return ZX_ERR_IO;

    size_t ret = read(fd_, buf, size);
    if (ret != size)
      return ZX_ERR_IO;
    return ZX_OK;
  }

  zx_status_t Write(off_t disk_offset, const void *buf, size_t size) override {
    fbl::AutoLock lock(&file_mutex_);
    off_t off = lseek(fd_, disk_offset, SEEK_SET);
    if (off < 0)
      return ZX_ERR_IO;

    size_t ret = write(fd_, buf, size);
    if (ret != size)
      return ZX_ERR_IO;
    return ZX_OK;
  }

  zx_status_t Submit() override { return ZX_OK; }

protected:
  fbl::Mutex file_mutex_;
  int fd_;
};

class DirectIOBlockDispatcher : public FdioBlockDispatcher {
public:
  static zx_status_t Create(int fd, size_t file_size, bool read_only, int vmid,
                            const PhysMem &phys_mem,
                            fbl::unique_ptr<BlockDispatcher> *out) {
    fbl::unique_fd ufd(fd);
    int vblock_id = GuestVmidToBlockBackendVmid(vmid);
    if (!IsBlockBackendVmid(vblock_id)) {
      FXL_LOG(ERROR) << "invalid block backend vmid: guest_vmid=" << vmid
                     << " backend_vmid=" << vblock_id;
      return ZX_ERR_INVALID_ARGS;
    }

    zx_handle_t guest_vmo = phys_mem.vmo().get();
    struct start_param start_par = {};
    start_par.vmid = static_cast<uint16_t>(vblock_id);
    start_par.mem.start = phys_mem.phys_base();
    start_par.mem.end = phys_mem.phys_base() + phys_mem.size() - 1;

    zx_status_t ret =
        fdio_ioctl(ufd.get(), IOCTL_VBLOCK_DIRECT_START, &start_par,
                   sizeof(start_par), nullptr, 0);
    if (ret < 0) {
      FXL_LOG(ERROR) << "block_dispatcher ioctl start failed, ret:" << ret;
      return ret;
    } else {
      FXL_LOG(INFO) << "block_dispatcher ioctl start succeed";
    }

    zx_handle_t vmo;
    ret = zx_handle_duplicate(guest_vmo, ZX_RIGHT_SAME_RIGHTS, &vmo);
    if (ret != ZX_OK) {
      FXL_LOG(ERROR) << "block_dispatcher, failed to duplicate handle, ret: "
                     << ret;
      return ret;
    }

    ret = fdio_ioctl(ufd.get(), IOCTL_VBLOCK_DIRECT_SET_GPA_RANGE, &vmo,
                     sizeof(vmo), nullptr, 0);
    if (ret < 0) {
      FXL_LOG(ERROR) << "block_dispatcher, ioctl set gpa failed, ret: " << ret;
      zx_handle_close(vmo);
      return ret;
    }
    zx_handle_close(vmo);

    FXL_LOG(INFO) << "block_dispatcher ioctl set gpa succeed, ret:" << ret;

    fbl::AllocChecker ac;
    auto dispatcher = fbl::make_unique_checked<DirectIOBlockDispatcher>(
        &ac, file_size, read_only, ufd.get(), vblock_id);
    if (!ac.check())
      return ZX_ERR_NO_MEMORY;

    dispatcher->blk_size_ = kSectorSize;
    dispatcher->max_seg_nums_ = 1;
    dispatcher->max_seg_size_ = kDefaultMaxSegSize;

    block_info_t info;
    ret = ioctl_block_get_info(ufd.get(), &info);
    if (ret != sizeof(info)) {
      FXL_LOG(ERROR) << "block_dispatcher ioctl get info failed";
      return ret;
    }

    dispatcher->size_ = info.block_count * info.block_size;
    dispatcher->blk_size_ = info.block_size;
    dispatcher->max_seg_nums_ = info.max_seg_nums;
    dispatcher->max_seg_size_ = info.max_transfer_size;
    FXL_LOG(INFO) << "max seg size: " << dispatcher->max_seg_size_;

    ufd.release();
    *out = fbl::move(dispatcher);
    return ZX_OK;
  }

  DirectIOBlockDispatcher(size_t size, bool read_only, int fd, int vmid)
      : FdioBlockDispatcher(size, read_only, fd), vmid_(vmid) {}

  zx_status_t Read(off_t disk_offset, void *buf, size_t size) override {
    fbl::AutoLock lock(&file_mutex_);

    struct direct_read_param {
      int32_t vmid;
      struct direct_io_rw_param rw;
    } param = {};
    param.vmid = vmid_;
    param.rw.len = static_cast<uint32_t>(size);
    param.rw.offset = static_cast<uint64_t>(disk_offset);
    param.rw.addr = reinterpret_cast<uint64_t>(buf);

    zx_status_t status =
        fdio_ioctl(fd_, IOCTL_VBLOCK_DIRECT_READ, &param,
                   sizeof(param), nullptr, 0);
    if (status < 0) {
      FXL_LOG(ERROR) << "block_dispatcher ioctl read failed, ret:" << status;
      return ZX_ERR_IO;
    }

    return ZX_OK;
  }

  zx_status_t ioctl(uint32_t request, void *buf, size_t size) override {
    fbl::AutoLock lock(&file_mutex_);
    return DoVblockIoctl(fd_, request, buf, size, "direct");
  }

private:
  const int vmid_;
};

struct PathLookupArgs {
  int fd;
  BlockDispatcher::Mode mode;
  const char *target_name;
};

static zx_status_t MatchBlockDeviceToPath(int dirfd, int event, const char *fn,
                                          void *cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }

  auto args = static_cast<PathLookupArgs *>(cookie);

  if (strcmp(fn, args->target_name) == 0) {
    fbl::unique_fd fd(
        openat(dirfd, fn,
               args->mode == BlockDispatcher::Mode::RO ? O_RDONLY : O_RDWR));

    if (!fd) {
      FXL_LOG(ERROR) << "block_dispatcher Failed to open device " << fn;
      return ZX_ERR_IO;
    }

    args->fd = fd.release();
    return ZX_ERR_STOP;
  }

  return ZX_OK;
}

zx_status_t BlockDispatcher::CreateFromPath(
    const char *path, const DispatcherOptions &options, const PhysMem &phys_mem,
    fbl::unique_ptr<BlockDispatcher> *dispatcher) {

  std::string path_str(path);
  size_t last_slash = path_str.find_last_of('/');

  std::string dir_path;
  std::string file_name;

  if (last_slash == std::string::npos) {
    dir_path = ".";
    file_name = path_str;
  } else {
    dir_path = (last_slash == 0) ? "/" : path_str.substr(0, last_slash);
    file_name = path_str.substr(last_slash + 1);
  }

  fbl::unique_fd dir_fd(open(dir_path.c_str(), O_DIRECTORY | O_RDONLY));
  if (!dir_fd) {
    FXL_LOG(ERROR) << "block_dispatcher Failed to open directory for watching: "
                   << dir_path;
    return ZX_ERR_IO;
  }

  PathLookupArgs args = {-1, options.mode, file_name.c_str()};

  FXL_LOG(INFO) << "block_dispatcher Waiting for block device node: "
                << file_name << " in " << dir_path;

  zx_status_t status = fdio_watch_directory(
      dir_fd.get(), MatchBlockDeviceToPath, ZX_TIME_INFINITE, &args);

  if (status == ZX_ERR_STOP) {
    return CreateFromFd(args.fd, options, phys_mem, dispatcher);
  }

  FXL_LOG(ERROR) << "block_dispatcher Failed to watch path: " << path
                 << ", status: " << status;
  return status;
}

struct GuidLookupArgs {
  int fd;
  BlockDispatcher::Mode mode;
  const BlockDispatcher::Guid &guid;
  ssize_t (*guid_ioctl)(int fd, void *out, size_t out_len);
};

static zx_status_t MatchBlockDeviceToGuid(int dirfd, int event, const char *fn,
                                          void *cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }
  auto args = static_cast<GuidLookupArgs *>(cookie);

  fbl::unique_fd fd(openat(
      dirfd, fn, args->mode == BlockDispatcher::Mode::RO ? O_RDONLY : O_RDWR));
  if (!fd) {
    FXL_LOG(ERROR) << "Failed to open device " << kBlockDirPath << "/" << fn;
    return ZX_ERR_IO;
  }

  uint8_t device_guid[GUID_LEN];
  ssize_t result = args->guid_ioctl(fd.get(), device_guid, sizeof(device_guid));
  if (result < 0) {
    return ZX_OK;
  }
  size_t device_guid_len = static_cast<size_t>(result);
  if (args->guid.empty() || sizeof(args->guid.bytes) != device_guid_len) {
    return ZX_OK;
  }
  if (memcmp(args->guid.bytes, device_guid, device_guid_len) != 0) {
    return ZX_OK;
  }
  args->fd = fd.release();
  return ZX_ERR_STOP;
}

zx_status_t BlockDispatcher::CreateFromGuid(
    const Guid &guid, zx_duration_t timeout, const DispatcherOptions &options,
    const PhysMem &phys_mem, fbl::unique_ptr<BlockDispatcher> *dispatcher) {
  GuidLookupArgs args = {-1, options.mode, guid, nullptr};
  switch (guid.type) {
  case GuidType::GPT_PARTITION_GUID:
    args.guid_ioctl = &ioctl_block_get_partition_guid;
    break;
  case GuidType::GPT_PARTITION_TYPE_GUID:
    args.guid_ioctl = &ioctl_block_get_type_guid;
    break;
  default:
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::unique_fd dir_fd(open(kBlockDirPath, O_DIRECTORY | O_RDONLY));
  if (!dir_fd) {
    return ZX_ERR_IO;
  }

  zx_status_t status = fdio_watch_directory(
      dir_fd.get(), MatchBlockDeviceToGuid, timeout, &args);
  if (status == ZX_ERR_STOP) {
    return CreateFromFd(args.fd, options, phys_mem, dispatcher);
  }
  return status;
}

class FifoBlockDispatcher;

class SubmissionQueue {
public:
  SubmissionQueue() = default;
  SubmissionQueue(zx_handle_t fifo_sub, zx_handle_t fifo_cmp,
                  FifoBlockDispatcher *disp)
      : fifo_sub_(fifo_sub), fifo_cmp_(fifo_cmp), disp_(disp) {}
  ~SubmissionQueue() = default;

  zx_status_t QueueBatch(const fifo_in_item* items, size_t count,
                         const uint64_t* cookies,
                         const uint32_t* req_ids);

  zx_status_t Queue(const void *addr, size_t size, uint32_t req_id,
                    uint64_t cookie);

private:
  friend class FifoBlockDispatcher;
  zx_handle_t fifo_sub_ = ZX_HANDLE_INVALID;
  zx_handle_t fifo_cmp_ = ZX_HANDLE_INVALID;
  FifoBlockDispatcher *disp_ = nullptr;
};

class FifoBlockDispatcher : public BlockDispatcher {
public:
  static zx_status_t Create(int fd, int vmid, const PhysMem &phys_mem,
                            fbl::unique_ptr<BlockDispatcher> *out) {
    fbl::unique_fd ufd(fd);
    int vblock_id = GuestVmidToBlockBackendVmid(vmid);
    if (!IsBlockBackendVmid(vblock_id)) {
      FXL_LOG(ERROR) << "invalid block backend vmid: guest_vmid=" << vmid
                     << " backend_vmid=" << vblock_id;
      return ZX_ERR_INVALID_ARGS;
    }

    struct mem_region mem = {
        .start = phys_mem.phys_base(),
        .end = phys_mem.phys_base() + phys_mem.size() - 1,
    };
    zx_handle_t guest_vmo = phys_mem.vmo().get();

    fbl::AllocChecker ac;
    auto dispatcher = fbl::make_unique_checked<FifoBlockDispatcher>(
        &ac, std::move(ufd), vblock_id, &mem, guest_vmo);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    if (dispatcher->init_status_ != ZX_OK) {
      return dispatcher->init_status_;
    }

    *out = fbl::move(dispatcher);
    return ZX_OK;
  }

  bool IsAsyncMode() const override { return true; }

  FifoBlockDispatcher(fbl::unique_fd fd, int vmid, const struct mem_region *mem,
                      zx_handle_t guest_vmo)
      : BlockDispatcher(0, false), fd_(std::move(fd)), vmid_(vmid), mem_(*mem),
        guest_vmo_(guest_vmo) {
    zx_status_t ret = zx_event_create(0, &slot_event_);
    if (ret != ZX_OK) {
      FXL_LOG(ERROR) << "block_dispatcher, failed to create slot event: "
                     << ret;
      init_status_ = ret;
      return;
    }

    ret = ioctl_block_get_info(fd_.get(), &info_);
    if (ret != sizeof(info_)) {
      FXL_LOG(ERROR) << "block_dispatcher ioctl get info failed";
      init_status_ = ret < 0 ? static_cast<zx_status_t>(ret) : ZX_ERR_IO;
      return;
    }
    FXL_LOG(INFO) << "block_dispatcher ioctl get info done, block_count: "
                  << info_.block_count;

    struct start_param start_par = {
        .vmid = static_cast<uint16_t>(vmid_),
        .mem = mem_,
    };

    ret = fdio_ioctl(fd_.get(), IOCTL_VBLOCK_START, &start_par,
                     sizeof(start_par), nullptr, 0);
    if (ret < 0) {
      FXL_LOG(ERROR) << "block_dispatcher ioctl start failed, ret: " << ret;
      init_status_ = ret;
      return;
    }

    int retries = 0;
    while (ZX_OK != fdio_ioctl(fd_.get(), IOCTL_VBLOCK_SET_GUEST, &vmid_,
                               sizeof(vmid_), nullptr, 0)) {
      if (++retries > kMaxIoctlRetries) {
        FXL_LOG(ERROR) << "block_dispatcher ioctl set guest timeout";
        init_status_ = ZX_ERR_TIMED_OUT;
        break;
      }
      zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
    }
    if (init_status_ != ZX_OK) {
      return;
    }

    uint64_t vblock_count = 0;
    ret = fdio_ioctl(fd_.get(), IOCTL_VBLOCK_GET_INFO, nullptr, 0,
                     &vblock_count, sizeof(vblock_count));
    if (ret < 0) {
      FXL_LOG(ERROR) << "block_dispatcher ioctl get info failed, ret: " << ret;
      init_status_ = ret;
      return;
    }
    if (vblock_count != 0) {
      info_.block_count = vblock_count;
    }
    FXL_LOG(INFO) << "block_dispatcher ioctl get vinfo done, vblock_count: "
                  << vblock_count << ", ret " << ret;

    zx_handle_t vmo = ZX_HANDLE_INVALID;
    ret = zx_handle_duplicate(guest_vmo_, ZX_RIGHT_SAME_RIGHTS, &vmo);
    if (ret != ZX_OK) {
      FXL_LOG(ERROR) << "block_dispatcher duplicate guest vmo failed, ret: "
                     << ret;
      init_status_ = ret;
      return;
    }

    ret = fdio_ioctl(fd_.get(), IOCTL_VBLOCK_SET_GPA_RANGE, &vmo, sizeof(vmo),
                     nullptr, 0);
    if (ret < 0) {
      FXL_LOG(ERROR) << "block_dispatcher ioctl set gpa failed, ret: " << ret;
      zx_handle_close(vmo);
      init_status_ = ret;
      return;
    }
    zx_handle_close(vmo);

    this->size_ = info_.block_count * info_.block_size;
    this->blk_size_ = info_.block_size;
    this->max_seg_nums_ = info_.max_seg_nums;
    this->max_seg_size_ = info_.max_transfer_size;

    zx_handle_t fifos[2] = {ZX_HANDLE_INVALID, ZX_HANDLE_INVALID};
    get_handle_param_t param = {
        .vmid = static_cast<uint16_t>(vmid),
        .fifo_id = 0,
    };
    ret = fdio_ioctl(fd_.get(), IOCTL_VBLOCK_GET_FIFO_HANDLES, &param,
                     sizeof(param), &fifos[0], sizeof(fifos[0]));
    if (ret < 0 || fifos[0] == ZX_HANDLE_INVALID) {
      FXL_LOG(ERROR) << "block_dispatcher ioctl get handle failed, ret: "
                     << ret;
      if (fifos[0] != ZX_HANDLE_INVALID) {
        zx_handle_close(fifos[0]);
      }
      init_status_ = ret < 0 ? static_cast<zx_status_t>(ret) : ZX_ERR_IO;
      return;
    }

    param.fifo_id = 1;
    ret = fdio_ioctl(fd_.get(), IOCTL_VBLOCK_GET_FIFO_HANDLES, &param,
                     sizeof(param), &fifos[1], sizeof(fifos[1]));
    if (ret < 0 || fifos[1] == ZX_HANDLE_INVALID) {
      FXL_LOG(ERROR) << "block_dispatcher ioctl get handle failed, ret: "
                     << ret;
      if (fifos[0] != ZX_HANDLE_INVALID) {
        zx_handle_close(fifos[0]);
      }
      if (fifos[1] != ZX_HANDLE_INVALID) {
        zx_handle_close(fifos[1]);
      }
      init_status_ = ret < 0 ? static_cast<zx_status_t>(ret) : ZX_ERR_IO;
      return;
    }

    sq_ = SubmissionQueue(fifos[0], fifos[1], this);
  }

  ~FifoBlockDispatcher() override {
    Shutdown();
    if (slot_event_ != ZX_HANDLE_INVALID) {
      zx_handle_close(slot_event_);
      slot_event_ = ZX_HANDLE_INVALID;
    }
  }

  zx_status_t Read(off_t disk_offset, void *buf, size_t size) override {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t Write(off_t disk_offset, const void *buf, size_t size) override {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t Flush() override { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t Submit() override { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t ioctl(uint32_t request, void *buf, size_t size) override {
    return DoVblockIoctl(fd_.get(), request, buf, size, "fifo");
  }

  void Shutdown() override {
    if (fd_) {
      zx_status_t ret =
          fdio_ioctl(fd_.get(), IOCTL_VBLOCK_STOP, &vmid_, sizeof(vmid_),
                     nullptr, 0);
      if (ret < 0) {
        FXL_LOG(WARNING) << "block_dispatcher@" << zx_log_guest_vmid(vmid_)
                         << ", ioctl stop failed: " << ret;
      }
    }
    if (sq_.fifo_sub_ != ZX_HANDLE_INVALID) {
      zx_handle_close(sq_.fifo_sub_);
      sq_.fifo_sub_ = ZX_HANDLE_INVALID;
    }
    if (sq_.fifo_cmp_ != ZX_HANDLE_INVALID) {
      zx_handle_close(sq_.fifo_cmp_);
      sq_.fifo_cmp_ = ZX_HANDLE_INVALID;
    }
  }

  zx_status_t SubmitBatch(const fifo_in_item* items, size_t count,
                          const uint64_t* cookies,
                          const uint32_t* req_ids) override {
    return sq_.QueueBatch(items, count, cookies, req_ids);
  }

  zx_status_t SubmitAsync(uint64_t addr, size_t size, uint32_t head,
                          uint64_t cookie) override {
    const fifo_in_item itm = {
        .addr = addr,
        .len = static_cast<uint32_t>(size),
        .head = head,
    };

    zx_status_t ret = sq_.Queue(&itm, sizeof(itm), head, cookie);
    if (ret != ZX_OK) {
      FXL_LOG(ERROR) << "block_dispatcher@" << zx_log_guest_vmid(vmid_)
                     << ", dispatcher failed to submit request";
    }
    return ret;
  }

  zx_status_t WaitForCompletionBatch(fifo_out_item* items,
                                     size_t max_count,
                                     size_t* count) override {
    while (true) {
      uint32_t actual = 0;
      zx_status_t ret = zx_fifo_read_old(sq_.fifo_cmp_, items,
                                         max_count * sizeof(fifo_out_item),
                                         &actual);
      if (ret == ZX_ERR_SHOULD_WAIT) {
        zx_signals_t signals;
        ret = zx_object_wait_one(sq_.fifo_cmp_,
                                 ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED,
                                 ZX_TIME_INFINITE, &signals);
        if (ret != ZX_OK) {
          if (ret == ZX_ERR_CANCELED) {
            FXL_LOG(INFO) << "block_dispatcher@" << zx_log_guest_vmid(vmid_)
                          << ", wait fifo read cancelled";
          } else {
            FXL_LOG(ERROR) << "block_dispatcher@" << zx_log_guest_vmid(vmid_)
                           << ", wait fifo read, not OK: " << ret;
          }
          return ret;
        }
        if (signals & ZX_FIFO_PEER_CLOSED) {
          FXL_LOG(INFO) << "block_dispatcher@" << zx_log_guest_vmid(vmid_)
                        << ", peer closed";
          return ZX_ERR_PEER_CLOSED;
        }
        continue;
      } else if (ret != ZX_OK) {
        if (ret == ZX_ERR_CANCELED) {
          FXL_LOG(INFO) << "block_dispatcher@" << zx_log_guest_vmid(vmid_)
                        << ", wait fifo read cancelled";
        } else {
          FXL_LOG(ERROR) << "block_dispatcher@" << zx_log_guest_vmid(vmid_)
                         << ", fifo read, not OK: " << ret;
        }
        return ret;
      }

      for (uint32_t i = 0; i < actual; ++i) {
        if (items[i].head >= pending_cookies_.size()) {
          FXL_LOG(ERROR) << "block_dispatcher@" << vmid_
                         << ", completion head out of range: "
                         << items[i].head;
          return ZX_ERR_OUT_OF_RANGE;
        }
      }

      *count = actual;
      return ZX_OK;
    }
  }

  uint64_t GetCompletionCookie(uint32_t head) const override {
    if (head >= pending_cookies_.size()) {
      FXL_LOG(ERROR) << "block_dispatcher@" << vmid_
                     << ", completion head out of range: " << head;
      return 0;
    }

    return pending_cookies_[head];
  }

  zx_status_t WaitForCompletion(uint32_t *head, uint64_t *cookie) override {
    while (true) {
      struct fifo_out_item itm;
      uint32_t actual = 0;

      zx_status_t ret =
          zx_fifo_read_old(sq_.fifo_cmp_, &itm, sizeof(itm), &actual);
      if (ret == ZX_ERR_SHOULD_WAIT) {
        zx_signals_t signals;
        ret = zx_object_wait_one(sq_.fifo_cmp_,
                                 ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED,
                                 ZX_TIME_INFINITE, &signals);
        if (ret != ZX_OK) {
          if (ret == ZX_ERR_CANCELED) {
            FXL_LOG(INFO) << "block_dispatcher@" << zx_log_guest_vmid(vmid_)
                          << ", wait fifo read cancelled";
          } else {
            FXL_LOG(ERROR) << "block_dispatcher@" << zx_log_guest_vmid(vmid_)
                           << ", wait fifo read, not OK: " << ret;
          }
          return ret;
        }
        if (signals & ZX_FIFO_PEER_CLOSED) {
          FXL_LOG(INFO) << "block_dispatcher@" << zx_log_guest_vmid(vmid_)
                        << ", peer closed";
          return ZX_ERR_PEER_CLOSED;
        }
        continue;
      } else if (ret != ZX_OK) {
        if (ret == ZX_ERR_CANCELED) {
          FXL_LOG(INFO) << "block_dispatcher@" << zx_log_guest_vmid(vmid_)
                        << ", wait fifo read cancelled";
        } else {
          FXL_LOG(ERROR) << "block_dispatcher@" << zx_log_guest_vmid(vmid_)
                         << ", fifo read, not OK: " << ret;
        }
        return ret;
      }

      if (itm.head >= pending_cookies_.size()) {
      FXL_LOG(ERROR) << "block_dispatcher@" << zx_log_guest_vmid(vmid_)
                       << ", completion head out of range: " << itm.head;
        return ZX_ERR_OUT_OF_RANGE;
      }

      *head = itm.head;
      *cookie = pending_cookies_[itm.head];
      return ZX_OK;
    }
  }

  void SetBlkConfig(virtio_blk_config_t &config, bool &crypto) override {
    config.capacity = info_.block_count * info_.block_size / kSectorSize;
    config.size_max = info_.max_transfer_size;
    config.seg_max = info_.max_seg_nums;
    config.geometry.cylinders =
        config.capacity / kDefaultGeometry / kDefaultGeometry;
    config.geometry.heads = kDefaultGeometry;
    config.geometry.sectors = kDefaultGeometry;
    config.blk_size = info_.block_size;
    config.num_queues = 1;
    config.max_discard_sectors = info_.discard_max_bytes / kSectorSize;
    config.max_discard_seg = info_.discard_max_segments;
    config.discard_sector_alignment = info_.discard_granularity / kSectorSize;
    config.max_write_zeroes_sectors =
        info_.write_zeroes_max_bytes / kSectorSize;
    config.max_write_zeroes_seg = info_.write_zeroes_max_segments;
    config.max_secure_erase_sectors =
        info_.secure_erase_max_bytes / kSectorSize;
    config.max_secure_erase_seg = info_.secure_erase_max_segments;
    config.secure_erase_sector_alignment =
        info_.secure_erase_granularity / kSectorSize;
    config.wce = info_.wce;
    config.ufs_lun = info_.block_id;

    if (info_.inline_crypto_supported) {
      config.crypto_cap = info_.crypto_cap;
      crypto = true;
    } else {
      crypto = false;
    }
  }

  void SetCtxStore(size_t num) override { pending_cookies_.resize(num); }

  int Vmid() const override { return vmid_; }

private:
  friend class SubmissionQueue;

  SubmissionQueue sq_;
  fbl::unique_fd fd_;
  const int vmid_;
  struct mem_region mem_;
  zx_handle_t guest_vmo_ = ZX_HANDLE_INVALID;
  block_info_t info_ = {};
  zx_handle_t slot_event_ = ZX_HANDLE_INVALID;
  std::vector<uint64_t> pending_cookies_;
  zx_status_t init_status_ = ZX_OK;
};

zx_status_t
BlockDispatcher::CreateFromFd(int fd, const DispatcherOptions &options,
                              const PhysMem &phys_mem,
                              fbl::unique_ptr<BlockDispatcher> *dispatcher) {
  fbl::unique_fd ufd(fd);
  off_t file_size = lseek(ufd.get(), 0, SEEK_END);
  if (file_size < 0) {
    FXL_LOG(ERROR) << "Failed to read size of block device";
    return ZX_ERR_IO;
  }

  bool read_only = options.mode == Mode::RO;
  switch (options.data_plane) {
  case DataPlane::FDIO:
    return FdioBlockDispatcher::Create(ufd.release(), file_size, read_only,
                                       phys_mem, dispatcher);
  case DataPlane::DIRECTIO:
    if (options.vmid == INT32_MAX) {
      FXL_LOG(ERROR) << "DIRECTIO data plane requires a valid vmid";
      return ZX_ERR_INVALID_ARGS;
    }
    return DirectIOBlockDispatcher::Create(ufd.release(), file_size, read_only,
                                           options.vmid, phys_mem, dispatcher);
  case DataPlane::FIFO:
    if (options.vmid == INT32_MAX) {
      FXL_LOG(ERROR) << "FIFO data plane requires a valid vmid";
      return ZX_ERR_INVALID_ARGS;
    }
    return FifoBlockDispatcher::Create(ufd.release(), options.vmid, phys_mem,
                                       dispatcher);
  default:
    FXL_LOG(ERROR) << "Unsupported block dispatcher data plane";
    return ZX_ERR_INVALID_ARGS;
  }
}

zx_status_t BlockDispatcher::CreateVolatileWrapper(
    fbl::unique_ptr<BlockDispatcher> dispatcher,
    fbl::unique_ptr<BlockDispatcher> *out) {
  return VolatileWriteBlockDispatcher::Create(fbl::move(dispatcher), out);
}

zx_status_t SubmissionQueue::QueueBatch(const fifo_in_item* items, size_t count,
                                        const uint64_t* cookies,
                                        const uint32_t* req_ids) {
  if (count == 0) {
    return ZX_OK;
  }

  for (size_t i = 0; i < count; ++i) {
    if (req_ids[i] >= disp_->pending_cookies_.size()) {
      FXL_LOG(ERROR) << "block_dispatcher@"
                     << zx_log_guest_vmid(disp_->vmid_)
                     << ", request id out of range: " << req_ids[i];
      return ZX_ERR_OUT_OF_RANGE;
    }

    disp_->pending_cookies_[req_ids[i]] = cookies[i];
  }

  size_t written = 0;
  while (written < count) {
    uint32_t actual = 0;
    zx_status_t ret = zx_fifo_write_old(
        fifo_sub_, items + written,
        (count - written) * sizeof(fifo_in_item), &actual);

    if (ret == ZX_OK) {
      written += actual;
      if (actual == 0) {
        FXL_LOG(WARNING) << "FIFO write returned 0 elements";
        break;
      }
    } else if (ret == ZX_ERR_SHOULD_WAIT) {
      zx_signals_t signals;
      ret = zx_object_wait_one(fifo_sub_,
                               ZX_FIFO_WRITABLE | ZX_FIFO_PEER_CLOSED,
                               ZX_TIME_INFINITE, &signals);
      if (ret != ZX_OK) {
        FXL_LOG(ERROR) << "Wait for FIFO writable failed: " << ret;
        return ret;
      }
      if (signals & ZX_FIFO_PEER_CLOSED) {
        FXL_LOG(ERROR) << "FIFO peer closed during batch write";
        return ZX_ERR_PEER_CLOSED;
      }
    } else {
      FXL_LOG(ERROR) << "FIFO write error: " << ret;
      return ret;
    }
  }
  return ZX_OK;
}

zx_status_t SubmissionQueue::Queue(const void *addr, size_t size,
                                   uint32_t req_id, uint64_t cookie) {
  if (req_id >= disp_->pending_cookies_.size()) {
    FXL_LOG(ERROR) << "block_dispatcher@"
                   << zx_log_guest_vmid(disp_->vmid_)
                   << ", request id out of range: " << req_id;
    return ZX_ERR_OUT_OF_RANGE;
  }

  disp_->pending_cookies_[req_id] = cookie;

  while (true) {
    uint32_t actual = 0;
    zx_status_t ret = zx_fifo_write_old(fifo_sub_, addr, size, &actual);

    if (ret == ZX_ERR_SHOULD_WAIT) {
      zx_signals_t signals;
      ret =
          zx_object_wait_one(fifo_sub_, ZX_FIFO_WRITABLE | ZX_FIFO_PEER_CLOSED,
                             ZX_TIME_INFINITE, &signals);
      if (ret != ZX_OK) {
        FXL_LOG(ERROR) << "block_dispatcher@"
                       << zx_log_guest_vmid(disp_->vmid_)
                       << ", wait write fifo not OK: " << ret;
        return ret;
      }
      if (signals & ZX_FIFO_PEER_CLOSED) {
        FXL_LOG(ERROR) << "block_dispatcher@"
                       << zx_log_guest_vmid(disp_->vmid_)
                       << ", wait write fifo peer closed";
        return ZX_ERR_PEER_CLOSED;
      }
      continue;
    }

    if (ret == ZX_OK) {
      if (actual < 1) {
        FXL_LOG(WARNING) << "block_dispatcher@"
                         << zx_log_guest_vmid(disp_->vmid_)
                         << ", write fifo actual < 1";
      }
    } else {
      FXL_LOG(ERROR) << "block_dispatcher@"
                     << zx_log_guest_vmid(disp_->vmid_)
                     << ", write fifo not OK: " << ret;
    }

    return ret;
  }
}

} // namespace machina
