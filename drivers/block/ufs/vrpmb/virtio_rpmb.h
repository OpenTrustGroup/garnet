/* SPDX-License-Identifier: BSD-3-Clause */

#pragma once

#include <stddef.h>

#include "ufs_host.h"
#include "virtio_rpmb_protocol.h"

zx_status_t ufs_virtio_rpmb_ioctl(mtk_ufs_t* ufs, uint32_t op, const void* in_buf, size_t in_len,
                                  void* out_buf, size_t out_len, size_t* out_actual);
zx_status_t ufs_virtio_rpmb_suspend(mtk_ufs_t* ufs, uint32_t flags);
zx_status_t ufs_virtio_rpmb_resume(mtk_ufs_t* ufs, uint32_t flags);
void ufs_virtio_rpmb_release(mtk_ufs_t* ufs);
