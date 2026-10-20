// SPDX-License-Identifier: BSD-3-Clause

#include "garnet/lib/littlefs/littlefs.h"
#include "garnet/lib/littlefs/vnode.h"

#include "lib/fxl/logging.h"
#include "lib/fxl/log_settings.h"

#include <zircon/types.h>
#include <threads.h>

extern thread_local uint64_t g_tls_lfs_write_blks;
extern bool enable_lfs_metrics;

namespace littlefs {

#define ROUNDUP(a, b) (((a) + ((b) - 1)) & ~((b) - 1))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

[[maybe_unused]] static void hexdump_ex(const void* ptr,
                                        size_t len,
                                        uint64_t disp_addr) {
  uintptr_t address = (uintptr_t)ptr;
  size_t count;

  for (count = 0; count < len; count += 16) {
    union {
      uint32_t buf[4];
      uint8_t cbuf[16];
    } u;
    size_t s = ROUNDUP(MIN(len - count, 16), 4);
    size_t i;

    printf(((disp_addr + len) > 0xFFFFFFFF) ? "0x%016" PRIx64 ": "
                                            : "0x%08" PRIx64 ": ",
           disp_addr + count);

    for (i = 0; i < s / 4; i++) {
      u.buf[i] = ((const uint32_t*)address)[i];
      printf("%08x ", u.buf[i]);
    }
    for (; i < 4; i++) {
      printf("         ");
    }
    printf("|");

    for (i = 0; i < 16; i++) {
      char c = u.cbuf[i];
      if (i < s && isprint(c)) {
        printf("%c", c);
      } else {
        printf(".");
      }
    }
    printf("|\n");
    address += 16;
  }
}

static int lfs_read(const struct lfs_config* c,
                    lfs_block_t block,
                    lfs_off_t off,
                    void* buffer,
                    lfs_size_t size) {
  auto* fs = static_cast<LittleFs*>(c->context);
  return fs->read(block, off, buffer, size);
}

static int lfs_write(const struct lfs_config* c,
                     lfs_block_t block,
                     lfs_off_t off,
                     const void* buffer,
                     lfs_size_t size) {
  g_tls_lfs_write_blks += (size / c->prog_size);
  auto* fs = static_cast<LittleFs*>(c->context);
  return fs->write(block, off, buffer, size);
}

static int lfs_erase(const struct lfs_config* c, lfs_block_t block) {
  return LFS_ERR_OK;
}

static int lfs_sync(const struct lfs_config* c) {
  auto* fs = static_cast<LittleFs*>(c->context);
  return fs->sync();
}

static mtx_t lfs_mutex_ = MTX_INIT;
static int lfs_lock(const struct lfs_config *c) __TA_ACQUIRE(&lfs_mutex_) {
  mtx_lock(&lfs_mutex_);
  return 0;
}

static int lfs_unlock(const struct lfs_config *c) __TA_RELEASE(&lfs_mutex_) {
  mtx_unlock(&lfs_mutex_);
  return 0;
}

zx_status_t LittleFs::MountAndServe(const options_t* options,
                                    async_t* async,
                                    zx::channel mount_channel) {

  if (options->verbose) {
    fxl::LogSettings log_settings = fxl::GetLogSettings();
    log_settings.min_log_level = static_cast<fxl::LogSeverity>(-1);
    fxl::SetLogSettings(log_settings);
  }

  enable_lfs_metrics = options->collect_metrics;

  int ret = lfs_mount(&lfs_, &cfg_);
  if (ret < 0) {
    FXL_LOG(ERROR) << "failed to mount filesystem: " << ret;
    return ZX_ERR_INTERNAL;
  }

  auto status = VnodeDir::Alloc(&root_dir_, &lfs_, fbl::String("."));
  if (status != ZX_OK) {
    return status;
  }

  SetReadonly(options->readonly);
  set_async(async);

  return ServeDirectory(root_dir_, fbl::move(mount_channel));
}

zx_status_t LittleFs::Format() {
  int ret = lfs_format(&lfs_, &cfg_);
  if (ret < 0) {
    FXL_LOG(ERROR) << "failed to format filesystem: " << ret;
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

zx_status_t LittleFs::Create(fbl::unique_ptr<Bcache> bc,
                             std::unique_ptr<LittleFs>* out) {
  auto fs = std::make_unique<LittleFs>();
  if (!fs) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status;
  if ((status = MappedVmo::Create(kBlockSize, "read_buffer", &fs->read_vmo_)) !=
      ZX_OK) {
    FXL_LOG(ERROR) << "failed to create read VMO: " << status;
    return status;
  }

  if ((status = bc->AttachVmo(fs->read_vmo_->GetVmo(), &fs->read_vmoid_)) !=
      ZX_OK) {
    FXL_LOG(ERROR) << "failed to attach write VMO: " << status;
    return status;
  }

  if ((status = MappedVmo::Create(kBlockSize, "write_buffer",
                                  &fs->write_vmo_)) != ZX_OK) {
    FXL_LOG(ERROR) << "failed to create write VMO: " << status;
    return status;
  }

  if ((status = bc->AttachVmo(fs->write_vmo_->GetVmo(), &fs->write_vmoid_)) !=
      ZX_OK) {
    FXL_LOG(ERROR) << "failed to attach write VMO: " << status;
    return status;
  }

  fs->cfg_.context = fs.get();
  fs->cfg_.read = lfs_read;
  fs->cfg_.prog = lfs_write;
  fs->cfg_.erase = lfs_erase;
  fs->cfg_.sync = lfs_sync;
  fs->cfg_.lock = lfs_lock;
  fs->cfg_.unlock = lfs_unlock;
  fs->cfg_.read_size = kBlockSize;
  fs->cfg_.prog_size = kBlockSize;
  fs->cfg_.block_size = kBlockSize;
  fs->cfg_.block_count = bc->Maxblk();
  fs->cfg_.cache_size = kBlockSize;
  fs->cfg_.lookahead_size = 32 * 1024;
  fs->cfg_.block_cycles = -1;  // disable wear leveling
  fs->bc_ = fbl::move(bc);

  *out = fbl::move(fs);
  return ZX_OK;
}

int LittleFs::read(lfs_block_t block,
                   lfs_off_t off,
                   void* buffer,
                   lfs_size_t size) {
  zx_status_t status;
  fs::ReadTxn<kBlockSize, Bcache> txn(bc_.get());
  txn.Enqueue(read_vmoid_, /*vmo_offset=*/0, block, /*nblocks=*/1);
  if ((status = txn.Flush()) != ZX_OK) {
    FXL_LOG(ERROR) << "failed to read block " << block << ": " << status;
    return LFS_ERR_IO;
  }

  memcpy(buffer, static_cast<uint8_t*>(read_vmo_->GetData()) + off, size);
  return LFS_ERR_OK;
}

int LittleFs::write(lfs_block_t block,
                    lfs_off_t off,
                    const void* buffer,
                    lfs_size_t size) {
  memcpy(static_cast<uint8_t*>(write_vmo_->GetData()) + off, buffer, size);

  zx_status_t status;
  fs::WriteTxn<kBlockSize, Bcache> txn(bc_.get());
  txn.Enqueue(write_vmoid_, /*vmo_offset=*/0, block, /*nblocks=*/1);
  if ((status = txn.Flush()) != ZX_OK) {
    FXL_LOG(ERROR) << "failed to write block " << block << ": " << status;
    return LFS_ERR_IO;
  }

  return LFS_ERR_OK;
}

int LittleFs::sync() {
  int ret = bc_->Sync();
  if (ret < 0) {
    FXL_LOG(ERROR) << "failed to sync block device: " << ret;
    return LFS_ERR_IO;
  }

  return LFS_ERR_OK;
}

}  // namespace littlefs