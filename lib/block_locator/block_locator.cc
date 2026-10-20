// Copyright 2026 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/block_locator/block_locator.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <fdio/watcher.h>
#include <zircon/device/block.h>

#include "lib/fxl/logging.h"

namespace block_locator {

namespace {

constexpr char kBlockClassPath[] = "/dev/class/block";
constexpr size_t kBlockNameMax = 128;

int open_flags(OpenMode mode) {
  return mode == OpenMode::kReadOnly ? O_RDONLY : O_RDWR;
}

struct GptNameLookupArgs {
  int fd = -1;
  const char* target_name = nullptr;
  OpenMode mode = OpenMode::kReadOnly;
};

zx_status_t MatchGptPartitionName(int dirfd, int event, const char* filename,
                                  void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }

  auto* args = static_cast<GptNameLookupArgs*>(cookie);
  fbl::unique_fd fd(openat(dirfd, filename, open_flags(args->mode)));
  if (!fd) {
    return ZX_OK;
  }

  uint8_t guid[GUID_LEN];
  if (ioctl_block_get_partition_guid(fd.get(), guid, sizeof(guid)) < 0) {
    return ZX_OK;
  }

  char name[kBlockNameMax] = {};
  if (ioctl_block_get_name(fd.get(), name, sizeof(name)) < 0 ||
      strcmp(name, args->target_name) != 0) {
    return ZX_OK;
  }

  FXL_LOG(INFO) << "block_locator resolved GPT partition name="
                << args->target_name << " class_node=" << filename;
  args->fd = fd.release();
  return ZX_ERR_STOP;
}

struct UfsLunLookupArgs {
  int fd = -1;
  uint16_t target_lun = 0;
  OpenMode mode = OpenMode::kReadOnly;
};

zx_status_t MatchUfsLun(int dirfd, int event, const char* filename,
                         void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }

  auto* args = static_cast<UfsLunLookupArgs*>(cookie);
  fbl::unique_fd fd(openat(dirfd, filename, open_flags(args->mode)));
  if (!fd) {
    return ZX_OK;
  }

  // GPT children retain the UFS block_id. Reject them so a user-LU lookup
  // cannot accidentally select a partition below the raw UFS device.
  uint8_t partition_guid[GUID_LEN];
  if (ioctl_block_get_partition_guid(fd.get(), partition_guid,
                                     sizeof(partition_guid)) >= 0) {
    return ZX_OK;
  }

  block_info_t info = {};
  if (ioctl_block_get_info(fd.get(), &info) != sizeof(info) ||
      !(info.flags & BLOCK_FLAG_BLOCK_ID_VALID) ||
      info.block_id != args->target_lun) {
    return ZX_OK;
  }

  FXL_LOG(INFO) << "block_locator resolved UFS LUN="
                << args->target_lun << " class_node=" << filename;
  args->fd = fd.release();
  return ZX_ERR_STOP;
}

template <typename Args>
zx_status_t WatchForBlockDevice(zx_status_t (*matcher)(int, int, const char*,
                                                       void*),
                                zx_duration_t timeout, Args* args,
                                fbl::unique_fd* out) {
  fbl::unique_fd dir_fd(open(kBlockClassPath, O_DIRECTORY | O_RDONLY));
  if (!dir_fd) {
    FXL_LOG(ERROR) << "block_locator failed to open " << kBlockClassPath;
    return ZX_ERR_IO;
  }

  zx_status_t status =
      fdio_watch_directory(dir_fd.get(), matcher, timeout, args);
  if (status != ZX_ERR_STOP) {
    return status;
  }

  out->reset(args->fd);
  return ZX_OK;
}

}  // namespace

zx_status_t OpenGptPartitionByName(const char* name, OpenMode mode,
                                   zx_duration_t timeout,
                                   fbl::unique_fd* out) {
  if (!name || name[0] == '\0' || !out) {
    return ZX_ERR_INVALID_ARGS;
  }

  FXL_LOG(INFO) << "block_locator waiting for GPT partition name=" << name;
  GptNameLookupArgs args = {.target_name = name, .mode = mode};
  zx_status_t status =
      WatchForBlockDevice(MatchGptPartitionName, timeout, &args, out);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "block_locator failed to resolve GPT partition name="
                   << name << " status=" << status;
  }
  return status;
}

zx_status_t OpenUfsLun(uint16_t lun, OpenMode mode, zx_duration_t timeout,
                       fbl::unique_fd* out) {
  if (!out) {
    return ZX_ERR_INVALID_ARGS;
  }

  FXL_LOG(INFO) << "block_locator waiting for UFS LUN=" << lun;
  UfsLunLookupArgs args = {.target_lun = lun, .mode = mode};
  zx_status_t status = WatchForBlockDevice(MatchUfsLun, timeout, &args, out);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "block_locator failed to resolve UFS LUN=" << lun
                   << " status=" << status;
  }
  return status;
}

}  // namespace block_locator
