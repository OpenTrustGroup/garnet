// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <fs/vfs.h>
#include <fs/vnode.h>
#include <memory>
#include <string>

#include "garnet/lib/littlefs/bcache.h"
#include "garnet/lib/littlefs/vnode.h"
#include "third_party/littlefs/lfs.h"

namespace littlefs {

typedef struct options {
  bool readonly;
  bool verbose;
  bool collect_metrics;
} options_t;

class LittleFs : public fs::Vfs {
 public:
  static zx_status_t Create(fbl::unique_ptr<Bcache> bc,
                            std::unique_ptr<LittleFs>* out);

  zx_status_t MountAndServe(const options_t* options,
                            async_t* async,
                            zx::channel mount_channel);

  zx_status_t Format();

  // hooks to lfs operations
  int read(lfs_block_t block, lfs_off_t off, void* buffer, lfs_size_t size);
  int write(lfs_block_t block,
            lfs_off_t off,
            const void* buffer,
            lfs_size_t size);
  int sync();

 private:
  lfs_t lfs_;
  lfs_config cfg_;
  fbl::unique_ptr<Bcache> bc_;
  fbl::RefPtr<VnodeDir> root_dir_;

  fbl::unique_ptr<MappedVmo> read_vmo_;
  fbl::unique_ptr<MappedVmo> write_vmo_;
  vmoid_t read_vmoid_;
  vmoid_t write_vmoid_;
};

}  // namespace littlefs