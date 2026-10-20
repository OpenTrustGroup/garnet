// SPDX-License-Identifier: NebulaProprietary
/*
* Copyright (c) 2025 MediaTek Inc.
* Copyright 2025 GoldenRiver Technologies Co., Ltd. All rights reserved.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <memory>
#include <stdio.h>
#include <unistd.h>

#include <virtio/virtio_ids.h>
#include <virtio/virtio_ring.h>
#include <zircon/compiler.h>
#include <trusty_std.h>
#include <fbl/unique_fd.h>

#include "lib/fxl/logging.h"
#include "garnet/lib/machina/virtio_pci.h"
#include "garnet/lib/machina/bits.h"
#include "garnet/lib/machina/virtio_device.h"
#include "garnet/lib/machina/virtio_queue.h"
#include "garnet/lib/machina/ipc_vqueue_fe.h"
#include "../../drivers/block/ufs/vrpmb/virtio_rpmb_protocol.h"

namespace machina {

static constexpr uint16_t kNumRpmbQueues = 1;
static constexpr uint16_t kRpmbQueueSize = 128;

#ifndef VIRTIO_ID_UFS_RPMB
#define VIRTIO_ID_UFS_RPMB		56 /* virtio ufs rpmb */
#endif

struct virtio_rpmb_config_t {};

struct virtio_rpmb_context {
	uint32_t region;
	uint32_t frame_num;
	ufs_virtio_rpmb_frame_t frame[];
};

class VirtioRpmb
    : public VirtioDeviceBase<VIRTIO_ID_UFS_RPMB, kNumRpmbQueues, virtio_rpmb_config_t> {
 public:
	VirtioRpmb(const PhysMem& phys_mem, uint16_t vmid,
	           Transport transport,
	           async_t* async = nullptr);
	VirtioRpmb() = delete;
	~VirtioRpmb();

 private:
	static zx_status_t CommandQueueHandler(VirtioQueue* queue,
						uint16_t head,
						uint32_t* used,
						void* ctx);

	zx_status_t HandleCommand(VirtioQueue* queue, uint16_t head, uint32_t* used);
	zx_status_t Start();
	void activate();
	void stop();

	async_t* async_;
	async::Wait command_queue_wait_;
	std::unique_ptr<async::Loop> loop_;
	fbl::unique_fd fd_;
	zx_handle_t shm_vmo_ = ZX_HANDLE_INVALID;
	zx_handle_t event_ = ZX_HANDLE_INVALID;
	uintptr_t shm_addr_ = 0;
	size_t shm_size_ = 0;
	uint16_t vmid_ = 0;
	bool started_ = false;
	mtx_t ipc_lock_;
	struct virtqueue_frontend ipc_vq_;
	ufs_virtio_rpmb_packet_t* packet_ = nullptr;

	zx_status_t InitBackend();
	void ReleaseBackend();
};

}  // namespace machina
