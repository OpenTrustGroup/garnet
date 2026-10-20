// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/bootloader/bootloader.h"

#include <block-client/client.h>
#include <fdio/watcher.h>
#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>

#include "garnet/bin/guest/bootloader/dummy.h"
#include "garnet/bin/guest/bootloader/linux.h"
#include "garnet/bin/guest/bootloader/lk2.h"
#include "garnet/lib/block_locator/block_locator.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/file_descriptor.h"
#include "lib/nebula/base/logging.h"

zx_status_t register_fast_block_io(const fbl::unique_fd& fd,
                                   zx_handle_t vmo,
                                   txnid_t* txnid_out,
                                   vmoid_t* vmoid_out,
                                   fifo_client_t** client_out) {
  zx::fifo fifo;
  if (ioctl_block_get_fifos(fd.get(), fifo.reset_and_get_address()) < 0) {
    LOG(ERROR) << "Couldn't attach fifo to partition";
    return ZX_ERR_IO;
  }
  if (ioctl_block_alloc_txn(fd.get(), txnid_out) < 0) {
    LOG(ERROR) << "Couldn't allocate transaction";
    return ZX_ERR_IO;
  }
  zx::vmo dup;
  if (zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS,
                          dup.reset_and_get_address()) != ZX_OK) {
    LOG(ERROR) << "Couldn't duplicate buffer vmo";
    return ZX_ERR_IO;
  }
  zx_handle_t h = dup.release();
  if (ioctl_block_attach_vmo(fd.get(), &h, vmoid_out) < 0) {
    LOG(ERROR) << "Couldn't attach VMO";
    return ZX_ERR_IO;
  }
  if (block_fifo_create_client(fifo.release(), client_out) != ZX_OK) {
    LOG(ERROR) << "Couldn't create block client";
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

static zx_status_t MatchBlockDeviceToPath(int dirfd, int event, const char* fn,
                                          void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }
  if (strcmp(fn, static_cast<const char*>(cookie)) == 0) {
    return ZX_ERR_STOP;
  }
  return ZX_OK;
}

static zx_status_t wait_for_block_device(const std::string& path) {
  static constexpr char kBlockDirPath[] = "/dev/class/block";
  static constexpr char kBlockDirPrefix[] = "/dev/class/block/";

  if (path.rfind(kBlockDirPrefix, 0) != 0) {
    return ZX_OK;
  }

  std::string file_name = path.substr(strlen(kBlockDirPrefix));
  fbl::unique_fd dir_fd(open(kBlockDirPath, O_DIRECTORY | O_RDONLY));
  if (!dir_fd) {
    LOG(ERROR) << "failed to open block directory: " << kBlockDirPath;
    return ZX_ERR_IO;
  }

  zx_status_t status =
      fdio_watch_directory(dir_fd.get(), MatchBlockDeviceToPath,
                           ZX_TIME_INFINITE, const_cast<char*>(file_name.c_str()));
  return status == ZX_ERR_STOP ? ZX_OK : status;
}

static zx_status_t load_image_from_file(machina::Guest& guest,
                                        uint64_t* out_offset,
                                        uint64_t* out_size,
                                        const nbl_vmm::Payload& payload) {
  fbl::unique_fd fd(open(payload.filepath().c_str(), O_RDONLY));
  if (!fd) {
    LOG(ERROR) << "failed to open bootloader payload file: "
               << payload.filepath();
    return ZX_ERR_IO;
  }

  uint64_t file_size;
  if (!files::GetFileSize(payload.filepath(), &file_size)) {
    LOG(ERROR) << "failed to get size of bootloader payload file: "
               << payload.filepath();
    return ZX_ERR_IO;
  }

  const machina::PhysMem& phys_mem = guest.phys_mem();
  auto buffer = phys_mem.as<char>(payload.offset(), file_size);
  ssize_t bytes_read =
      fxl::ReadFileDescriptor(fd.get(), buffer, file_size);
  if (bytes_read < 0 || static_cast<uint64_t>(bytes_read) != file_size) {
    LOG(ERROR) << "failed to read payload from file: " << payload.filepath();
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

static zx_status_t load_image_from_block_device(
    machina::Guest& guest,
    uint64_t* out_offset,
    uint64_t* out_size,
    const nbl_vmm::Payload& payload) {
  fbl::unique_fd fd;
  if (!payload.partition_name().empty()) {
    zx_status_t status = block_locator::OpenGptPartitionByName(
        payload.partition_name().c_str(), block_locator::OpenMode::kReadOnly,
        ZX_TIME_INFINITE, &fd);
    if (status != ZX_OK) {
      LOG(ERROR) << "failed to resolve bootloader payload GPT partition: "
                 << payload.partition_name();
      return status;
    }
  } else {
    auto wait_status = wait_for_block_device(payload.filepath());
    if (wait_status != ZX_OK) {
      LOG(ERROR) << "failed to wait for bootloader payload block device: "
                 << payload.filepath();
      return wait_status;
    }
    fd.reset(open(payload.filepath().c_str(), O_RDONLY));
  }

  if (!fd) {
    LOG(ERROR) << "failed to open bootloader payload file: "
               << (payload.partition_name().empty() ? payload.filepath()
                                                    : payload.partition_name());
    return ZX_ERR_IO;
  }

  const machina::PhysMem& phys_mem = guest.phys_mem();
  zx_handle_t ram_vmo = phys_mem.vmo().get();

  auto ram_size = phys_mem.size();
  int64_t offset = payload.offset();
  if (offset < 0) {
    offset = ram_size + offset;
  }

  block_info_t info;
  auto status = ioctl_block_get_info(fd.get(), &info);
  if (status != sizeof(block_info_t)) {
    LOG(ERROR) << "failed to get block info";
    return ZX_ERR_IO;
  }

  if (offset % info.block_size != 0) {
    LOG(ERROR) << "payload load offset not aligned to block size";
    return ZX_ERR_INVALID_ARGS;
  }

  txnid_t txnid;
  vmoid_t vmoid;
  fifo_client_t* client;
  status = register_fast_block_io(fd, ram_vmo, &txnid, &vmoid, &client);
  if (status != ZX_OK) {
    LOG(ERROR) << "failed to register fast block io";
    return status;
  }

  block_fifo_request_t request;
  request.txnid = txnid;
  request.vmoid = vmoid;
  request.opcode = BLOCKIO_READ;
  request.length = info.block_count;
  request.vmo_offset = offset / info.block_size;
  request.dev_offset = 0;

  status = block_fifo_txn(client, &request, 1);
  if (status == ZX_OK) {
    *out_offset = offset;
    *out_size = info.block_count * info.block_size;
  } else {
    LOG(ERROR) << "failed to read payload";
  }

  block_fifo_release_client(client);
  if (status == ZX_OK && payload.header_size()) {
    uint64_t total_size = info.block_count * info.block_size;
    const uint64_t copy_src_offset = payload.header_size();
    if (total_size > copy_src_offset) {
      uint64_t copy_size = total_size - copy_src_offset;

      void* src = phys_mem.as<void>(offset + copy_src_offset, total_size);
      void* dest = phys_mem.as<void>(offset, total_size);
      memmove(dest, src, copy_size);
      LOG(INFO) << "copy : " << std::hex << copy_src_offset << " - "
                << total_size << " to: " << std::hex << offset;
    } else {
        LOG(ERROR) << "fail" << status;
    }
  }
  return status;
}

static zx_status_t load_image(machina::Guest& guest,
                              uint64_t* out_offset,
                              uint64_t* out_size,
                              const nbl_vmm::Payload& payload) {
  if (!payload.partition_name().empty()) {
    if (!payload.filepath().empty()) {
      LOG(ERROR) << "bootloader payload cannot specify both filepath and "
                    "partition_name";
      return ZX_ERR_INVALID_ARGS;
    }
    return load_image_from_block_device(guest, out_offset, out_size, payload);
  }

  if (payload.filepath().empty()) {
    LOG(ERROR) << "bootloader payload is missing filepath or partition_name";
    return ZX_ERR_INVALID_ARGS;
  }

  if (files::IsFile(payload.filepath())) {
    return load_image_from_file(guest, out_offset, out_size, payload);
  } else {
    return load_image_from_block_device(guest, out_offset, out_size, payload);
  }
}

// static
std::unique_ptr<Bootloader> Bootloader::BuildFromProto(std::string path,
                                                       machina::Guest& guest) {
  auto fd = open(path.c_str(), O_RDONLY);
  CHECK(fd > 0);

  google::protobuf::io::FileInputStream fin(fd);
  fin.SetCloseOnDelete(true);
  nbl_vmm::BootloaderRecord rec;
  auto success = google::protobuf::TextFormat::Parse(&fin, &rec);
  CHECK(success);

  std::unique_ptr<Bootloader> loader;
  if (rec.protocol() == "linux")
    loader = std::make_unique<BootloaderLinux>(rec, guest);
  else if (rec.protocol() == "lk2")
    loader = std::make_unique<BootloaderLk2>(rec, guest);
  else if (rec.protocol() == "dummy")
    loader = std::make_unique<BootloaderDummy>(rec, guest);
  else
    CHECK(false) << "unknown bootloader protocol " << rec.protocol();

  return loader;
}

Bootloader::Bootloader(nbl_vmm::BootloaderRecord& rec, machina::Guest& guest)
    : guest_(guest), phys_mem_(guest_.phys_mem()) {
  for (auto& payload : rec.payload()) {
    uint64_t offset, size;
    auto status = load_image(guest_, &offset, &size, payload);
    CHECK(status == ZX_OK) << "failed to load payload";

    loaded_images_.push_back(
        {payload.name(), phys_mem_.phys_base() + offset, size});
  }
}
