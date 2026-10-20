# When using UFS debugfs
When using UFS debugfs, you need to include the corresponding deps
and include_dirs in the BUILD.gn file:
deps = [
    "//garnet/public/lib/blk_ufsinfo",
  ]
include_dirs = [ "//garnet/public/lib",
  ]

# Additionally, include the header file:

#include "blk_ufsinfo/set_info.h"

# Example of calling the interface

You can refer to the following example when calling the interface:

char _data[256];
snprintf(_data, 256, "ufs-block-info=block_size: %d\nblock_count:
          %lu\nmax_transfer_size: %u\nmax_seg_nums: %u\n",
    UFS_BLOCK_SIZE,
    ufs->block_info.block_count,
    ufs->block_info.max_transfer_size,
    ufs->block_info.max_seg_nums);

zx_status_t ret;
ret = storage_device_info(_data);
if (ret) {
    zxlogf(ERROR, "%s: Failed to storage info, ret %d\n", __func__, ret);
}

# Note:
The part left of the = (ufs-block-info) represents the name; the part
right of the = (block_size: %d\n...) represents the parameters.

This feature can store up to 40 pieces of information. For details,
see public/lib/blk_ufsinfo/set_info.h:
#define INFO_MAX_ENTRIES 40
#define INFO_NAME_LEN 32
#define INFO_VALUE_LEN 128