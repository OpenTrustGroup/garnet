// Copyright 2018 Open Trust Group.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Copyright (c) 2013-2016, Google, Inc. All rights reserved
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <fbl/auto_call.h>

#include "garnet/bin/gzos/smc_service/trusty_smc.h"
#include "garnet/lib/gzos/trusty_virtio/trusty_virtio_device.h"

#include "lib/fxl/arraysize.h"

typedef uint64_t ns_addr_t;
typedef uint32_t ns_size_t;
typedef uint64_t ns_paddr_t;

// clang-format off
/* Normal memory */
#define NS_MAIR_NORMAL_CACHED_WB_RWA       0xFF /* inner and outer write back read/write allocate */
#define NS_MAIR_NORMAL_CACHED_WT_RA        0xAA /* inner and outer write through read allocate */
#define NS_MAIR_NORMAL_CACHED_WB_RA        0xEE /* inner and outer wriet back, read allocate */
#define NS_MAIR_NORMAL_UNCACHED            0x44 /* uncached */

/* sharaeble attributes */
#define NS_NON_SHAREABLE                   0x0
#define NS_OUTER_SHAREABLE                 0x2
#define NS_INNER_SHAREABLE                 0x3
// clang-format on

typedef struct ns_page_info {
  /* 48-bit physical address 47:12 */
  ns_paddr_t pa() { return attr & 0xFFFFFFFFF000ULL; }

  /* sharaeble attributes */
  uint64_t shareable() { return (attr >> 8) & 0x3; }

  /* cache attrs encoded in the top bits 55:49 of the PTE*/
  uint64_t mair() { return (attr >> 48) & 0xFF; }

  /* Access permissions AP[2:1]
   *    EL0   EL1
   * 00 None  RW
   * 01 RW    RW
   * 10 None  RO
   * 11 RO    RO
   */
  uint64_t ap() { return ((attr) >> 6) & 0x3; }
  bool ap_user() { return ap() & 0x1 ? true : false; }
  bool ap_ro() { return ap() & 0x2 ? true : false; }

  uint64_t attr;
} ns_page_info_t;

namespace trusty_virtio {
constexpr trusty_vdev_descr kVdevDescriptors[] = {
    DECLARE_TRUSTY_VIRTIO_DEVICE_DESCR(kTipcDeviceId, "dev0", 32, 32),
};
}

namespace smc_service {

using trusty_virtio::kVdevDescriptors;
using trusty_virtio::trusty_vdev_descr;
using trusty_virtio::TrustyVirtioDevice;
using trusty_virtio::VirtioBus;
using trusty_virtio::VirtioDevice;

TrustySmcEntity::TrustySmcEntity(async_t* async, zx::channel ch,
                                 fbl::RefPtr<SharedMem> shm)
    : async_(async), shared_mem_(shm), vbus_(nullptr) {
  ree_message_.Bind(fbl::move(ch));
}

zx_status_t TrustySmcEntity::Init() {
  // Create VirtioBus
  vbus_ = fbl::make_unique<trusty_virtio::VirtioBus>(shared_mem_);
  if (vbus_ == nullptr) {
    FXL_LOG(ERROR) << "Failed to create virtio bus object";
    return ZX_ERR_NO_MEMORY;
  }

  auto delete_vbus = fbl::MakeAutoCall([&]() { vbus_.reset(nullptr); });

  fidl::VectorPtr<ree_agent::MessageChannelInfo> ch_infos;

  zx_status_t status = ZX_OK;
  // Create trusty virtio devices and add to virtio bus
  for (uint32_t i = 0; i < arraysize(kVdevDescriptors); i++) {
    const trusty_vdev_descr* desc = &kVdevDescriptors[i];

    zx::channel h1, h2;
    status = zx::channel::create(0, &h1, &h2);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create ree message channel";
      return status;
    }

    if (desc->vdev.id != trusty_virtio::kTipcDeviceId) {
      FXL_LOG(ERROR) << "Unsupported virtio device id: " << desc->vdev.id;
      return ZX_ERR_NOT_SUPPORTED;
    }

    auto vdev = fbl::MakeRefCounted<TrustyVirtioDevice>(kVdevDescriptors[i],
                                                        async_, fbl::move(h2));
    if (vdev == nullptr) {
      FXL_LOG(ERROR) << "Failed to create trusty virtio device object: "
                     << desc->config.dev_name;
      return ZX_ERR_NO_MEMORY;
    }

    ree_agent::MessageChannelInfo ch_info{
        ree_agent::MessageType::Tipc, vdev->notify_id(),
        desc->config.msg_buf_max_size, fbl::move(h1)};
    ch_infos.push_back(fbl::move(ch_info));

    status = vbus_->AddDevice(vdev);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to add trusty virtio device to virtio bus: "
                     << desc->config.dev_name << " (" << status << ")";
      return status;
    }
  }

  bool res = ree_message_->AddMessageChannel(std::move(ch_infos), &status);
  if (!res) {
    FXL_LOG(ERROR) << "Failed to invoke AddMessageChannel fidl function";
    return ZX_ERR_INTERNAL;
  }

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to AddMessageChannel, status=" << status;
    return status;
  }

  delete_vbus.cancel();
  return ZX_OK;
}

/* TODO(james): Currently we do not support dynamically mapping ns buffer.
 *              Do we still need to check memory attribute here?
 */
static zx_status_t check_mem_attr(ns_page_info_t pi, bool is_shm_use_cache) {
  bool is_mem_cached;

  switch (pi.mair()) {
    case NS_MAIR_NORMAL_CACHED_WB_RWA:
      is_mem_cached = true;
      break;
    case NS_MAIR_NORMAL_UNCACHED:
      is_mem_cached = false;
      break;
    default:
      FXL_DLOG(ERROR) << "Unsupported memory attr: 0x" << std::hex << pi.mair();
      return ZX_ERR_NOT_SUPPORTED;
  }

  if (is_shm_use_cache != is_mem_cached) {
    FXL_DLOG(ERROR) << "memory cache attribute is not aligned with ns share "
                       "memory cache policy";
    return ZX_ERR_INVALID_ARGS;
  }

  if (is_mem_cached) {
    if (pi.shareable() != NS_INNER_SHAREABLE) {
      FXL_DLOG(ERROR) << "Unsupported sharable attr: 0x" << std::hex
                      << pi.shareable();
      return ZX_ERR_NOT_SUPPORTED;
    }
  }

  if (pi.ap_user() || pi.ap_ro()) {
    FXL_DLOG(ERROR) << "Unexpected access permission: 0x" << std::hex
                    << pi.ap();
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

zx_status_t TrustySmcEntity::GetNsBuf(smc32_args_t* args, void** buf,
                                      size_t* size) {
  FXL_DCHECK(args != nullptr);
  FXL_DCHECK(size != nullptr);

  ns_paddr_t ns_buf_pa;
  ns_size_t ns_buf_sz;

  ns_page_info_t pi = {
      .attr = (static_cast<uint64_t>(args->params[1]) << 32) | args->params[0],
  };

  zx_status_t status = check_mem_attr(pi, shared_mem_->use_cache());
  if (status != ZX_OK) {
    return status;
  }

  ns_buf_pa = pi.pa();
  ns_buf_sz = static_cast<ns_size_t>(args->params[2]);

  void* tmp_buf = shared_mem_->PhysToVirt<void*>(ns_buf_pa, ns_buf_sz);
  if (tmp_buf == nullptr) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  *buf = tmp_buf;
  *size = static_cast<size_t>(ns_buf_sz);
  return ZX_OK;
}

/*
 * Translate internal errors to SMC errors
 */
static long to_smc_error(long ret) {
  if (ret > 0)
    return ret;

  switch (ret) {
    case ZX_OK:
      return SM_OK;

    case ZX_ERR_INVALID_ARGS:
      return SM_ERR_INVALID_PARAMETERS;

    case ZX_ERR_NOT_SUPPORTED:
      return SM_ERR_NOT_SUPPORTED;

    case ZX_ERR_BAD_STATE:
    case ZX_ERR_OUT_OF_RANGE:
    case ZX_ERR_ACCESS_DENIED:
    case ZX_ERR_IO:
      return SM_ERR_NOT_ALLOWED;

    default:
      return SM_ERR_INTERNAL_FAILURE;
  }
}

zx_status_t TrustySmcEntity::InvokeNopFunction(smc32_args_t* args) {
  zx_status_t status;

  switch (args->params[0]) {
    case SMC_NC_VDEV_KICK_VQ:
      status = vbus_->KickVqueue(args->params[1], args->params[2]);
      break;

    default:
      FXL_DLOG(ERROR) << "unknown nop function: "
                      << SMC_FUNCTION(args->params[0]);
      status = ZX_ERR_NOT_SUPPORTED;
  }

  return status;
}

long TrustySmcEntity::InvokeSmcFunction(smc32_args_t* args) {
  zx_status_t status;
  void* ns_buf;
  size_t size;
  bool ret;

  switch (args->smc_nr) {
    case SMC_SC_NOP:
      status = InvokeNopFunction(args);
      break;

    case SMC_SC_VIRTIO_GET_DESCR:
      status = GetNsBuf(args, &ns_buf, &size);
      if (status == ZX_OK) {
        status = vbus_->GetResourceTable(ns_buf, &size);
      }

      if (status == ZX_OK) {
        status = size;
      }
      break;

    case SMC_SC_VIRTIO_START:
      if ((status = GetNsBuf(args, &ns_buf, &size)) != ZX_OK)
        break;
      if ((status = vbus_->Start(ns_buf, size)) != ZX_OK)
        break;

      ret = ree_message_->Start(nullptr, &status);
      if (!ret || (status != ZX_OK)) {
        if (!ret) {
          FXL_LOG(ERROR) << "Failed to invoke ree_message Start() function";
        }
        vbus_->Stop(ns_buf, size);
      }
      break;

    case SMC_SC_VIRTIO_STOP:
      if ((status = GetNsBuf(args, &ns_buf, &size)) != ZX_OK)
        break;
      if ((status = vbus_->Stop(ns_buf, size)) != ZX_OK)
        break;

      ret = ree_message_->Stop(nullptr, &status);
      if (!ret) {
        FXL_LOG(ERROR) << "Failed to invoke ree_message Stop() function";
        status = ZX_ERR_INTERNAL;
      }
      break;

    case SMC_SC_VDEV_RESET:
      if ((status = vbus_->ResetDevice(args->params[0])) != ZX_OK)
        break;

      {
        fidl::VectorPtr<uint32_t> ids;
        ids.push_back(args->params[0]);
        ret = ree_message_->Stop(fbl::move(ids), &status);
        if (!ret) {
          FXL_LOG(ERROR) << "Failed to invoke ree_message Stop() function";
          status = ZX_ERR_INTERNAL;
        }
      }
      break;

    case SMC_SC_VDEV_KICK_VQ:
      status = vbus_->KickVqueue(args->params[0], args->params[1]);
      break;

    default:
      FXL_DLOG(ERROR) << "unknown smc function: " << SMC_FUNCTION(args->smc_nr);
      status = ZX_ERR_NOT_SUPPORTED;
      break;
  }

  return to_smc_error(status);
}

}  // namespace smc_service
