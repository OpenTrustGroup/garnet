// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_gpu.h"

#include <string.h>

#include <fbl/intrusive_hash_table.h>
#include <fbl/unique_ptr.h>

#include "garnet/lib/machina/gpu_bitmap.h"
#include "garnet/lib/machina/gpu_resource.h"
#include "garnet/lib/machina/gpu_scanout.h"
#include "lib/fxl/logging.h"

namespace machina {

VirtioGpu::VirtioGpu(const PhysMem& phys_mem, async_t* async)
    : VirtioDeviceBase(phys_mem), async_(async) {}

VirtioGpu::~VirtioGpu() = default;

zx_status_t VirtioGpu::Init() {
  zx_status_t status = control_queue()->PollAsync(
      async_, &control_queue_wait_, &VirtioGpu::QueueHandler, this);
  if (status == ZX_OK) {
    status = cursor_queue()->PollAsync(async_, &cursor_queue_wait_,
                                       &VirtioGpu::QueueHandler, this);
  }
  return status;
}

zx_status_t VirtioGpu::AddScanout(GpuScanout* scanout) {
  if (scanout_ != nullptr) {
    return ZX_ERR_ALREADY_EXISTS;
  }

  {
    fbl::AutoLock lock(&config_mutex_);
    FXL_DCHECK(config_.num_scanouts == 0);
    config_.num_scanouts = 1;
  }
  scanout_ = scanout;
  return ZX_OK;
}

zx_status_t VirtioGpu::QueueHandler(VirtioQueue* queue,
                                    uint16_t head,
                                    uint32_t* used,
                                    void* ctx) {
  VirtioGpu* gpu = reinterpret_cast<VirtioGpu*>(ctx);
  return gpu->HandleGpuCommand(queue, head, used);
}

zx_status_t VirtioGpu::HandleGpuCommand(VirtioQueue* queue,
                                        uint16_t head,
                                        uint32_t* used) {
  virtio_desc_t request_desc;
  queue->ReadDesc(head, &request_desc);
  auto request_header =
      reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(request_desc.addr);

  // Cursor commands don't send a response (at least not in the linux driver).
  if (!request_desc.has_next &&
      request_header->type != VIRTIO_GPU_CMD_MOVE_CURSOR &&
      request_header->type != VIRTIO_GPU_CMD_UPDATE_CURSOR) {
    FXL_LOG(ERROR) << "Command " << request_header->type
                   << " does not contain a response descriptor";
    return ZX_OK;
  }

  virtio_gpu_ctrl_hdr_t* response_header = nullptr;
  switch (request_header->type) {
    case VIRTIO_GPU_CMD_GET_DISPLAY_INFO: {
      virtio_desc_t response_desc;
      queue->ReadDesc(request_desc.next, &response_desc);
      auto request =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_resp_display_info_t*>(response_desc.addr);
      response_header = &response->hdr;
      GetDisplayInfo(request, response);
      *used += sizeof(*response);
      break;
    }
    case VIRTIO_GPU_CMD_RESOURCE_CREATE_2D: {
      virtio_desc_t response_desc;
      queue->ReadDesc(request_desc.next, &response_desc);
      auto request =
          reinterpret_cast<virtio_gpu_resource_create_2d_t*>(request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      response_header = response;
      ResourceCreate2D(request, response);
      *used += sizeof(*response);
      break;
    }
    case VIRTIO_GPU_CMD_SET_SCANOUT: {
      virtio_desc_t response_desc;
      queue->ReadDesc(request_desc.next, &response_desc);
      auto request =
          reinterpret_cast<virtio_gpu_set_scanout_t*>(request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      response_header = response;
      SetScanout(request, response);
      *used += sizeof(*response);
      break;
    }
    case VIRTIO_GPU_CMD_RESOURCE_FLUSH: {
      virtio_desc_t response_desc;
      queue->ReadDesc(request_desc.next, &response_desc);
      auto request =
          reinterpret_cast<virtio_gpu_resource_flush_t*>(request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      response_header = response;
      ResourceFlush(request, response);
      *used += sizeof(*response);
      break;
    }
    case VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D: {
      virtio_desc_t response_desc;
      queue->ReadDesc(request_desc.next, &response_desc);
      auto request = reinterpret_cast<virtio_gpu_transfer_to_host_2d_t*>(
          request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      response_header = response;
      TransferToHost2D(request, response);
      *used += sizeof(*response);
      break;
    }
    case VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING: {
      virtio_desc_t response_desc;
      queue->ReadDesc(request_desc.next, &response_desc);

      // This may or may not be on the same descriptor.
      virtio_gpu_mem_entry_t* mem_entries;
      if (response_desc.has_next) {
        mem_entries =
            reinterpret_cast<virtio_gpu_mem_entry_t*>(response_desc.addr);
        queue->ReadDesc(response_desc.next, &response_desc);
      } else {
        uintptr_t addr = reinterpret_cast<uintptr_t>(request_desc.addr) +
                         sizeof(virtio_gpu_resource_attach_backing_t);
        mem_entries = reinterpret_cast<virtio_gpu_mem_entry_t*>(addr);
      }

      auto request = reinterpret_cast<virtio_gpu_resource_attach_backing_t*>(
          request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      response_header = response;
      ResourceAttachBacking(request, mem_entries, response);
      *used += sizeof(*response);
      break;
    }
    case VIRTIO_GPU_CMD_RESOURCE_UNREF: {
      virtio_desc_t response_desc;
      queue->ReadDesc(request_desc.next, &response_desc);
      auto request =
          reinterpret_cast<virtio_gpu_resource_unref_t*>(request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      response_header = response;
      ResourceUnref(request, response);
      *used += sizeof(*response);
      break;
    }
    case VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING: {
      virtio_desc_t response_desc;
      queue->ReadDesc(request_desc.next, &response_desc);
      auto request = reinterpret_cast<virtio_gpu_resource_detach_backing_t*>(
          request_desc.addr);
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      response_header = response;
      ResourceDetachBacking(request, response);
      *used += sizeof(*response);
      break;
    }
    case VIRTIO_GPU_CMD_UPDATE_CURSOR:
    case VIRTIO_GPU_CMD_MOVE_CURSOR: {
      auto request =
          reinterpret_cast<virtio_gpu_update_cursor_t*>(request_desc.addr);
      MoveOrUpdateCursor(request);
      *used = 0;
      break;
    }
    default: {
      FXL_LOG(ERROR) << "Unsupported GPU command " << request_header->type;
      // ACK.
      virtio_desc_t response_desc;
      queue->ReadDesc(request_desc.next, &response_desc);
      auto response =
          reinterpret_cast<virtio_gpu_ctrl_hdr_t*>(response_desc.addr);
      response_header = response;
      response->type = VIRTIO_GPU_RESP_ERR_UNSPEC;
      *used += sizeof(*response);
      break;
    }
  }
  if (response_header && request_header->flags & VIRTIO_GPU_FLAG_FENCE) {
    // Virtio 1.0 (GPU) Section 5.7.6.7:
    //
    // If the driver sets the VIRTIO_GPU_FLAG_FENCE bit in the request flags
    // field the device MUST:
    //
    // * set VIRTIO_GPU_FLAG_FENCE bit in the response,
    // * copy the content of the fence_id field from the request to the
    //   response, and
    // * send the response only after command processing is complete.
    //
    // Note: VirtioQueue::PollAsync runs sequentially so fences are naturally
    // enforced.
    response_header->flags |= VIRTIO_GPU_FLAG_FENCE;
    response_header->fence_id = request_header->fence_id;
  }
  return ZX_OK;
}

void VirtioGpu::GetDisplayInfo(const virtio_gpu_ctrl_hdr_t* request,
                               virtio_gpu_resp_display_info_t* response) {
  virtio_gpu_display_one_t* display = &response->pmodes[0];
  if (scanout_ == nullptr) {
    memset(display, 0, sizeof(*display));
    response->hdr.type = VIRTIO_GPU_RESP_ERR_UNSPEC;
    return;
  }

  display->enabled = 1;
  display->r.x = 0;
  display->r.y = 0;
  display->r.width = scanout_->width();
  display->r.height = scanout_->height();
  response->hdr.type = VIRTIO_GPU_RESP_OK_DISPLAY_INFO;
}

void VirtioGpu::ResourceCreate2D(const virtio_gpu_resource_create_2d_t* request,
                                 virtio_gpu_ctrl_hdr_t* response) {
  fbl::unique_ptr<GpuResource> res = GpuResource::Create(request, this);
  if (!res) {
    response->type = VIRTIO_GPU_RESP_ERR_UNSPEC;
    return;
  }
  resources_.insert(fbl::move(res));
  response->type = VIRTIO_GPU_RESP_OK_NODATA;
}

void VirtioGpu::ResourceUnref(const virtio_gpu_resource_unref_t* request,
                              virtio_gpu_ctrl_hdr_t* response) {
  auto it = resources_.find(request->resource_id);
  if (it == resources_.end()) {
    response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
    return;
  }
  resources_.erase(it);
  response->type = VIRTIO_GPU_RESP_OK_NODATA;
}

void VirtioGpu::SetScanout(const virtio_gpu_set_scanout_t* request,
                           virtio_gpu_ctrl_hdr_t* response) {
  if (request->resource_id == 0) {
    // Resource ID 0 is a special case and means the provided scanout
    // should be disabled.
    scanout_->SetResource(nullptr, request);
    response->type = VIRTIO_GPU_RESP_OK_NODATA;
    return;
  }
  if (request->scanout_id != 0 || scanout_ == nullptr) {
    // Only a single scanout is supported.
    response->type = VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID;
    return;
  }

  auto res = resources_.find(request->resource_id);
  if (res == resources_.end()) {
    response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
    return;
  }
  scanout_->SetResource(&*res, request);

  response->type = VIRTIO_GPU_RESP_OK_NODATA;
}

void VirtioGpu::ResourceAttachBacking(
    const virtio_gpu_resource_attach_backing_t* request,
    const virtio_gpu_mem_entry_t* mem_entries,
    virtio_gpu_ctrl_hdr_t* response) {
  auto it = resources_.find(request->resource_id);
  if (it == resources_.end()) {
    response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
    return;
  }
  response->type = it->AttachBacking(mem_entries, request->nr_entries);
}

void VirtioGpu::ResourceDetachBacking(
    const virtio_gpu_resource_detach_backing_t* request,
    virtio_gpu_ctrl_hdr_t* response) {
  auto it = resources_.find(request->resource_id);
  if (it == resources_.end()) {
    response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
    return;
  }
  response->type = it->DetachBacking();
}

void VirtioGpu::TransferToHost2D(
    const virtio_gpu_transfer_to_host_2d_t* request,
    virtio_gpu_ctrl_hdr_t* response) {
  auto it = resources_.find(request->resource_id);
  if (it == resources_.end()) {
    response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
    return;
  }
  response->type = it->TransferToHost2D(request);
}

void VirtioGpu::ResourceFlush(const virtio_gpu_resource_flush_t* request,
                              virtio_gpu_ctrl_hdr_t* response) {
  auto it = resources_.find(request->resource_id);
  if (it == resources_.end()) {
    response->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
    return;
  }
  response->type = it->Flush(request);
}

void VirtioGpu::MoveOrUpdateCursor(const virtio_gpu_update_cursor_t* request) {
  bool is_update = request->hdr.type == VIRTIO_GPU_CMD_UPDATE_CURSOR;
  GpuResource* resource = nullptr;
  if (is_update && request->resource_id != 0) {
    auto it = resources_.find(request->resource_id);
    if (it == resources_.end()) {
      return;
    }
    resource = &*it;
  }
  if (request->pos.scanout_id != 0 || scanout_ == nullptr) {
    // Only a single scanout is supported.
    return;
  }
  scanout_->MoveOrUpdateCursor(resource, request);
}

}  // namespace machina
