// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "virtio_rpmb.h"

#include <endian.h>
#include <fcntl.h>
#include <fdio/io.h>
#include <functional>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <cstddef>
#include <cstring>

namespace machina {

namespace {

constexpr char kRpmbBackendPath[] = "/dev/sys/platform/64:16:1/sdc/vrpmb";
constexpr size_t kRpmbContextHeaderBytes = offsetof(virtio_rpmb_context, frame);

const char* RpmbReqRespToString(uint16_t req_resp) {
  switch (req_resp) {
    case 0x0001:
      return "key-program";
    case 0x0002:
      return "read-counter";
    case 0x0003:
      return "authenticated-write";
    case 0x0004:
      return "authenticated-read";
    case 0x0005:
      return "result-read";
    case 0x0006:
      return "sec-wpcb-write";
    case 0x0007:
      return "sec-wpcb-read";
    case 0x0100:
      return "key-program-rsp";
    case 0x0200:
      return "read-counter-rsp";
    case 0x0300:
      return "authenticated-write-rsp";
    case 0x0400:
      return "authenticated-read-rsp";
    case 0x0600:
      return "sec-wpcb-write-rsp";
    case 0x0700:
      return "sec-wpcb-read-rsp";
    default:
      return "unknown";
  }
}

const char* HandleTypeToString(uint32_t type) {
  constexpr uint32_t kObjTypeVmo = 3u;
  constexpr uint32_t kObjTypeEventPair = 16u;

  switch (type) {
    case kObjTypeVmo:
      return "vmo";
    case kObjTypeEventPair:
      return "eventpair";
    default:
      return "unknown";
  }
}

size_t IpcVirtqBytes(uint32_t depth) {
  return sizeof(struct virtio_common_zone) + sizeof(struct virtq_desc) * depth +
         (((sizeof(struct virtq_avail) + sizeof(uint16_t) * depth) + 7) & -8) +
         sizeof(struct virtq_used) + sizeof(uint16_t) +
         sizeof(struct virtq_used_elem) * depth;
}

void ResetRpmbResponse(virtio_rpmb_context* rsp,
                       size_t frame_bytes,
                       uint32_t region,
                       uint32_t frame_num,
                       uint32_t* used) {
  if (rsp != nullptr) {
    std::memset(rsp->frame, 0, frame_bytes);
    rsp->region = region;
    rsp->frame_num = frame_num;
  }
  if (used != nullptr) {
    *used = kRpmbContextHeaderBytes;
  }
}

}  // namespace

VirtioRpmb::~VirtioRpmb() {
  ReleaseBackend();
  if (loop_) {
    loop_->Quit();
    loop_->JoinThreads();
  }
}

void VirtioRpmb::activate() {
  zx_status_t status = Start();
  FXL_DCHECK(status == ZX_OK);
}

void VirtioRpmb::stop() {}

zx_status_t VirtioRpmb::Start() {
  if (started_) {
    return ZX_OK;
  }

  FXL_LOG(INFO) << "RPMB frontend start: vmid=" << vmid_;
  zx_status_t status = this->queue(0)->PollAsync(async_, &command_queue_wait_,
                                                 &VirtioRpmb::CommandQueueHandler, this);
  if (status == ZX_OK) {
    started_ = true;
  }
  return status;
}

zx_status_t VirtioRpmb::InitBackend() {
  ufs_virtio_rpmb_start_param_t start = {
      .vmid = vmid_,
  };
  ufs_virtio_rpmb_get_handle_param_t get_handle = {
      .vmid = vmid_,
      .handle_id = UFS_VIRTIO_RPMB_SHM_HANDLE_ID,
  };
  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_status_t status;
  int ret;

  FXL_LOG(INFO) << "RPMB backend init begin: vmid=" << vmid_ << " path=" << kRpmbBackendPath;

  for (int retry = 0; retry < 100 && access(kRpmbBackendPath, F_OK) != 0; ++retry) {
    usleep(100000);
  }

  fd_.reset(open(kRpmbBackendPath, O_RDONLY));
  if (!fd_) {
    FXL_LOG(ERROR) << "failed to open RPMB backend: " << kRpmbBackendPath;
    return ZX_ERR_NOT_FOUND;
  }

  ret = fdio_ioctl(fd_.get(), GRT_VRPMB_IOCTL_START, &start, sizeof(start), nullptr, 0);
  if (ret != 0) {
    FXL_LOG(ERROR) << "failed to start RPMB backend session: " << ret;
    return ZX_ERR_INTERNAL;
  }

  ret = fdio_ioctl(fd_.get(), GRT_VRPMB_IOCTL_GET_HANDLE, &get_handle, sizeof(get_handle),
                   &handle, sizeof(handle));
  if (ret != sizeof(handle)) {
    FXL_LOG(ERROR) << "failed to get RPMB shared memory handle: " << ret;
    return ZX_ERR_INTERNAL;
  }
  zx_info_handle_basic_t shm_info = {};
  status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &shm_info, sizeof(shm_info), nullptr,
                              nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to inspect RPMB shared memory handle: handle=" << handle
                   << " status=" << status;
    return status;
  }
  FXL_LOG(INFO) << "RPMB shared memory handle: handle=" << handle
                << " type=" << HandleTypeToString(shm_info.type)
                << " rights=0x" << std::hex << shm_info.rights << std::dec;
  if (shm_info.type != 3u) {
    FXL_LOG(ERROR) << "RPMB shared memory handle has unexpected type: " << shm_info.type;
    return ZX_ERR_WRONG_TYPE;
  }
  shm_vmo_ = handle;

  uint64_t size = 0;
  status = zx_vmo_get_size(shm_vmo_, &size);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to query RPMB shared memory size: " << status;
    return status;
  }
  shm_size_ = static_cast<size_t>(size);

  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, 0,
                       shm_vmo_, 0, shm_size_, &shm_addr_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to map RPMB shared memory: " << status;
    return status;
  }

  get_handle.handle_id = UFS_VIRTIO_RPMB_EVENT_HANDLE_ID;
  handle = ZX_HANDLE_INVALID;
  ret = fdio_ioctl(fd_.get(), GRT_VRPMB_IOCTL_GET_HANDLE, &get_handle, sizeof(get_handle),
                   &handle, sizeof(handle));
  if (ret != sizeof(handle)) {
    FXL_LOG(ERROR) << "failed to get RPMB event handle: " << ret;
    return ZX_ERR_INTERNAL;
  }
  zx_info_handle_basic_t event_info = {};
  status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &event_info, sizeof(event_info),
                              nullptr, nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to inspect RPMB event handle: handle=" << handle
                   << " status=" << status;
    return status;
  }
  FXL_LOG(INFO) << "RPMB event handle: handle=" << handle
                << " type=" << HandleTypeToString(event_info.type)
                << " rights=0x" << std::hex << event_info.rights << std::dec;
  event_ = handle;

  if (virtq_frontend_init(&ipc_vq_, reinterpret_cast<void*>(shm_addr_), shm_size_,
                          UFS_VIRTIO_RPMB_QUEUE_DEPTH, event_) != 0) {
    FXL_LOG(ERROR) << "failed to initialize RPMB IPC virtqueue";
    return ZX_ERR_INTERNAL;
  }

  packet_ = reinterpret_cast<ufs_virtio_rpmb_packet_t*>(
      shm_addr_ + IpcVirtqBytes(UFS_VIRTIO_RPMB_QUEUE_DEPTH));
  FXL_LOG(INFO) << "RPMB backend init done: vmid=" << vmid_ << " shm_size=" << shm_size_;
  return ZX_OK;
}

void VirtioRpmb::ReleaseBackend() {
  FXL_LOG(INFO) << "RPMB backend release: vmid=" << vmid_;
  if (fd_) {
    ufs_virtio_rpmb_start_param_t stop = {
        .vmid = vmid_,
    };
    fdio_ioctl(fd_.get(), GRT_VRPMB_IOCTL_STOP, &stop, sizeof(stop), nullptr, 0);
  }
  if (shm_addr_ != 0 && shm_size_ != 0) {
    zx_vmar_unmap(zx_vmar_root_self(), shm_addr_, shm_size_);
  }
  if (event_ != ZX_HANDLE_INVALID) {
    zx_handle_close(event_);
  }
  if (shm_vmo_ != ZX_HANDLE_INVALID) {
    zx_handle_close(shm_vmo_);
  }

  shm_addr_ = 0;
  shm_size_ = 0;
  event_ = ZX_HANDLE_INVALID;
  shm_vmo_ = ZX_HANDLE_INVALID;
  packet_ = nullptr;
  fd_.reset();
}

VirtioRpmb::VirtioRpmb(const PhysMem& phys_mem, uint16_t vmid,
                       Transport transport,
                       async_t* async)
    : VirtioDeviceBase(phys_mem, transport), vmid_(vmid) {
  mtx_init(&ipc_lock_, mtx_plain);

  if (transport == Transport::MMIO) {
    auto mmio_dev = mmio_device();
    mmio_dev->RegisterDriverReadyHandler(std::bind(&VirtioRpmb::activate, this));
    mmio_dev->RegisterDriverResetHandler(std::bind(&VirtioRpmb::stop, this));
  }

  if (async != nullptr) {
    async_ = async;
  } else {
    loop_ = std::make_unique<async::Loop>();
    FXL_DCHECK(loop_ != nullptr);
    loop_->StartThread("virtio-vmrpmb");
    async_ = loop_->async();
  }

  zx_status_t status = InitBackend();
  FXL_DCHECK(status == ZX_OK);

  if (transport != Transport::MMIO) {
    status = Start();
    FXL_DCHECK(status == ZX_OK);
  }
}

zx_status_t VirtioRpmb::CommandQueueHandler(VirtioQueue* queue,
                                            uint16_t head,
                                            uint32_t* used,
                                            void* ctx) {
  auto thiz = reinterpret_cast<VirtioRpmb*>(ctx);
  return thiz->HandleCommand(queue, head, used);
}

zx_status_t VirtioRpmb::HandleCommand(VirtioQueue* queue, uint16_t head, uint32_t* used) {
  virtio_desc_t req_desc;
  virtio_desc_t rsp_desc;
  virtio_rpmb_context* rpmb_req = nullptr;
  virtio_rpmb_context* rpmb_rsp = nullptr;
  ufs_virtio_rpmb_vq_req_t ipc_req = {};
  ufs_virtio_rpmb_vq_req_t ipc_rsp = {};
  uint32_t req_id = 0;
  void* user_ctx = nullptr;
  uint32_t req_frames = 0;
  uint32_t rsp_frames = 0;
  uint32_t region = RPMB_REGION_0;
  uint16_t msg_type = 0;
  size_t req_bytes;
  size_t rsp_bytes;
  size_t req_capacity;
  size_t rsp_capacity;

  *used = 0;

  zx_status_t status = queue->ReadDesc(head, &req_desc);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to read RPMB request descriptor: " << status;
    return ZX_OK;
  }
  if (!req_desc.has_next) {
    FXL_LOG(ERROR) << "RPMB request missing response descriptor";
    return ZX_OK;
  }

  status = queue->ReadDesc(req_desc.next, &rsp_desc);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to read RPMB response descriptor: " << status;
    return ZX_OK;
  }

  if (req_desc.writable || !rsp_desc.writable) {
    FXL_LOG(ERROR) << "RPMB descriptor permissions are invalid";
    return ZX_OK;
  }

  if (req_desc.len < kRpmbContextHeaderBytes) {
    FXL_LOG(ERROR) << "RPMB request descriptor is too small";
    return ZX_OK;
  }

  if (rsp_desc.len < kRpmbContextHeaderBytes) {
    FXL_LOG(ERROR) << "RPMB response descriptor is too small";
    return ZX_OK;
  }

  req_capacity = req_desc.len - kRpmbContextHeaderBytes;
  rsp_capacity = rsp_desc.len - kRpmbContextHeaderBytes;

  rpmb_req = reinterpret_cast<virtio_rpmb_context*>(req_desc.addr);
  rpmb_rsp = reinterpret_cast<virtio_rpmb_context*>(rsp_desc.addr);
  if (rpmb_req == nullptr || rpmb_rsp == nullptr) {
    FXL_LOG(ERROR) << "RPMB descriptor address is invalid";
    return ZX_OK;
  }

  req_frames = rpmb_req->frame_num;
  rsp_frames = rpmb_rsp->frame_num;
  region = rpmb_req->region;
  if (req_frames == 0 || rsp_frames == 0 || req_frames > UFS_VIRTIO_RPMB_MAX_FRAMES ||
      rsp_frames > UFS_VIRTIO_RPMB_MAX_FRAMES) {
    FXL_LOG(ERROR) << "RPMB frame count is invalid";
    ResetRpmbResponse(rpmb_rsp, rsp_capacity, region, 0, used);
    return ZX_OK;
  }

  req_bytes = req_frames * sizeof(ufs_virtio_rpmb_frame_t);
  rsp_bytes = rsp_frames * sizeof(ufs_virtio_rpmb_frame_t);
  if (req_capacity < req_bytes) {
    FXL_LOG(ERROR) << "RPMB request buffer is too small";
    ResetRpmbResponse(rpmb_rsp, rsp_capacity, region, 0, used);
    return ZX_OK;
  }
  if (rsp_capacity < rsp_bytes) {
    FXL_LOG(ERROR) << "RPMB response buffer is too small";
    ResetRpmbResponse(rpmb_rsp, rsp_capacity, region, 0, used);
    return ZX_OK;
  }
  if (region > RPMB_REGION_3) {
    FXL_LOG(ERROR) << "RPMB region is invalid: " << region;
    ResetRpmbResponse(rpmb_rsp, rsp_capacity, region, 0, used);
    return ZX_OK;
  }

  mtx_lock(&ipc_lock_);

  msg_type = be16toh(rpmb_req->frame[0].req_resp);
  FXL_LOG(ERROR) << "RPMB request begin: vmid=" << vmid_ << " head=" << head
                << " region=" << region << " type=" << msg_type
                << "(" << RpmbReqRespToString(msg_type) << ")"
                << " req_frames=" << req_frames << " rsp_frames=" << rsp_frames;

  std::memset(packet_, 0, sizeof(*packet_));
  packet_->magic = UFS_VIRTIO_RPMB_PACKET_MAGIC;
  packet_->region = region;
  packet_->req_nfrm = req_frames;
  packet_->rsp_nfrm = rsp_frames;
  packet_->req_bytes = static_cast<uint32_t>(req_bytes);
  packet_->rsp_bytes = static_cast<uint32_t>(rsp_bytes);
  std::memcpy(packet_->req_frame, rpmb_req->frame, req_bytes);

  ipc_req.slot_id = 0;
  ipc_req.region = region;
  ipc_req.req_nfrm = req_frames;
  ipc_req.rsp_nfrm = rsp_frames;
  ipc_req.req_bytes = static_cast<uint32_t>(req_bytes);
  ipc_req.rsp_bytes = static_cast<uint32_t>(rsp_bytes);

  if (virtq_frontend_to_avail(&ipc_vq_, &ipc_req, sizeof(ipc_req), 0, nullptr) != 0) {
    FXL_LOG(ERROR) << "failed to queue RPMB IPC request";
    ResetRpmbResponse(rpmb_rsp, rsp_capacity, region, 0, used);
    mtx_unlock(&ipc_lock_);
    return ZX_OK;
  }

  if (vq_frontend_wait_event(&ipc_vq_, true) != 0) {
    FXL_LOG(ERROR) << "timed out waiting for RPMB IPC response";
    ResetRpmbResponse(rpmb_rsp, rsp_capacity, region, 0, used);
    mtx_unlock(&ipc_lock_);
    return ZX_OK;
  }

  if (virtq_frontend_get_used(&ipc_vq_, &ipc_rsp, &req_id, &user_ctx) != 0) {
    FXL_LOG(ERROR) << "failed to dequeue RPMB IPC response";
    ResetRpmbResponse(rpmb_rsp, rsp_capacity, region, 0, used);
    mtx_unlock(&ipc_lock_);
    return ZX_OK;
  }

  rpmb_rsp->region = packet_->region;
  rpmb_rsp->frame_num = packet_->rsp_nfrm;
  if (packet_->rsp_nfrm > UFS_VIRTIO_RPMB_MAX_FRAMES || packet_->rsp_bytes > rsp_capacity) {
    FXL_LOG(ERROR) << "RPMB IPC response size is invalid";
    ResetRpmbResponse(rpmb_rsp, rsp_capacity, packet_->region, 0, used);
    mtx_unlock(&ipc_lock_);
    return ZX_OK;
  }
  std::memcpy(rpmb_rsp->frame, packet_->rsp_frame, packet_->rsp_bytes);
  *used = kRpmbContextHeaderBytes + packet_->rsp_bytes;

  status = packet_->status;
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "RPMB backend request failed: status=" << status
                   << " rpmb_ret=" << packet_->rpmb_ret
                   << " result=" << packet_->rpmb_result;
  } else {
    FXL_LOG(INFO) << "RPMB request done: vmid=" << vmid_ << " head=" << head
                  << " region=" << packet_->region << " type=" << msg_type
                  << "(" << RpmbReqRespToString(msg_type) << ")"
                  << " rsp_frames=" << packet_->rsp_nfrm << " rsp_bytes=" << packet_->rsp_bytes
                  << " result=" << packet_->rpmb_result;
  }
  mtx_unlock(&ipc_lock_);
  return ZX_OK;
}

}  // namespace machina
