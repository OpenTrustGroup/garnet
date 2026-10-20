/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Copyright (c) 2026 MediaTek Inc.
 */

#include "virtio_rpmb.h"

#include <stdalign.h>
#include <trusty_std.h>

#include "ipc_vqueue_be.h"
#include "ufs_interface.h"
#include "ufs_rpmb.h"
#include "garnet/lib/vm_id/vm_id.h"

#define UFS_VIRTIO_RPMB_THREAD_PRIORITY 17
#define PERF_BIND_CPU_TEMPORARILY 0

typedef struct ufs_virtio_rpmb_session {
    bool in_use;
    bool stop_worker;
    bool worker_running;
    u16 vmid;
    zx_handle_t shm_vmo;
    zx_handle_t event[2];
    uintptr_t shm_vaddr;
    size_t shm_size;
    thrd_t worker;
    mtx_t lock;
    struct virtqueue_backend vq;
} ufs_virtio_rpmb_session_t;

typedef struct ufs_virtio_rpmb_backend {
    bool initialized;
    bool suspended;
    uint32_t inflight_reqs;
    mtx_t lock;
    ufs_virtio_rpmb_session_t sessions[UFS_VIRTIO_RPMB_MAX_GUESTS];
} ufs_virtio_rpmb_backend_t;

static ufs_virtio_rpmb_backend_t g_ufs_virtio_rpmb_backend;

static const char* ufs_virtio_rpmb_req_resp_to_str(u16 req_resp) {
    switch (req_resp) {
    case RPMB_REQ_AUTHEN_KEY_PRG:
        return "key-program";
    case RPMB_REQ_READ_WR_CNT:
        return "read-counter";
    case RPMB_REQ_AUTHEN_WRITE:
        return "authenticated-write";
    case RPMB_REQ_AUTHEN_READ:
        return "authenticated-read";
    case RPMB_REQ_RESULT_RD:
        return "result-read";
    case RPMB_REQ_SEC_WPCB_WRITE:
        return "sec-wpcb-write";
    case RPMB_REQ_SEC_WPCB_READ:
        return "sec-wpcb-read";
    default:
        return "unknown";
    }
}

static size_t ufs_virtio_rpmb_virtq_bytes(uint32_t depth) {
    return sizeof(struct virtio_common_zone) + sizeof(struct virtq_desc) * depth +
           (((sizeof(struct virtq_avail) + sizeof(uint16_t) * depth) + 7) & -8) +
           sizeof(struct virtq_used) + sizeof(uint16_t) +
           sizeof(struct virtq_used_elem) * depth;
}

static size_t ufs_virtio_rpmb_packet_bytes(void) {
    size_t packet_bytes = sizeof(ufs_virtio_rpmb_packet_t);
    size_t align_bytes = alignof(ufs_virtio_rpmb_packet_t);

    return (packet_bytes + align_bytes - 1) & ~(align_bytes - 1);
}

static size_t ufs_virtio_rpmb_shm_bytes(void) {
    return ufs_virtio_rpmb_virtq_bytes(UFS_VIRTIO_RPMB_QUEUE_DEPTH) +
           ufs_virtio_rpmb_packet_bytes() * UFS_VIRTIO_RPMB_QUEUE_DEPTH;
}

static ufs_virtio_rpmb_packet_t* ufs_virtio_rpmb_packet(ufs_virtio_rpmb_session_t* session,
                                                        uint32_t slot_id) {
    uintptr_t base = session->shm_vaddr + ufs_virtio_rpmb_virtq_bytes(UFS_VIRTIO_RPMB_QUEUE_DEPTH);
    size_t packet_bytes = ufs_virtio_rpmb_packet_bytes();

    if (slot_id >= UFS_VIRTIO_RPMB_QUEUE_DEPTH) {
        return NULL;
    }

    return (ufs_virtio_rpmb_packet_t*)(base + packet_bytes * slot_id);
}

static zx_status_t ufs_virtio_rpmb_ret_to_status(int ret) {
    if (ret == -UFS_RPMB_RESPONSE_ERROR) {
        return ZX_ERR_IO_DATA_INTEGRITY;
    }
    if (ret == -UFS_RPMB_DRIVER_ERROR) {
        return ZX_ERR_INTERNAL;
    }
    if (ret == -UFS_RPMB_HMAC_ERROR || ret == -UFS_RPMB_NONCE_ERROR ||
        ret == -UFS_RPMB_WRCNT_ERROR) {
        return ZX_ERR_IO_DATA_INTEGRITY;
    }
    return ZX_ERR_IO;
}

static void ufs_virtio_rpmb_backend_init_once(void) {
    ufs_virtio_rpmb_backend_t* backend = &g_ufs_virtio_rpmb_backend;
    uint32_t index;

    if (backend->initialized) {
        return;
    }

    mtx_init(&backend->lock, mtx_plain);
    backend->suspended = false;
    backend->inflight_reqs = 0;
    for (index = 0; index < UFS_VIRTIO_RPMB_MAX_GUESTS; ++index) {
        ufs_virtio_rpmb_session_t* session = &backend->sessions[index];

        memset(session, 0, sizeof(*session));
        session->shm_vmo = ZX_HANDLE_INVALID;
        session->event[0] = ZX_HANDLE_INVALID;
        session->event[1] = ZX_HANDLE_INVALID;
        mtx_init(&session->lock, mtx_plain);
    }

    backend->initialized = true;
    UFS_LOGI("rpmb backend initialized: max_guests=%u queue_depth=%u\n",
             UFS_VIRTIO_RPMB_MAX_GUESTS, UFS_VIRTIO_RPMB_QUEUE_DEPTH);
}

static void ufs_virtio_rpmb_session_reset(ufs_virtio_rpmb_session_t* session) {
    bool was_in_use = session->in_use;
    u16 vmid = session->vmid;

    if (session->shm_vaddr != 0 && session->shm_size != 0) {
        zx_vmar_unmap(zx_vmar_root_self(), session->shm_vaddr, session->shm_size);
    }
    if (session->shm_vmo != ZX_HANDLE_INVALID) {
        zx_handle_close(session->shm_vmo);
    }
    if (session->event[0] != ZX_HANDLE_INVALID) {
        zx_handle_close(session->event[0]);
    }
    if (session->event[1] != ZX_HANDLE_INVALID) {
        zx_handle_close(session->event[1]);
    }

    memset(&session->vq, 0, sizeof(session->vq));
    session->in_use = false;
    session->stop_worker = false;
    session->worker_running = false;
    session->vmid = GRT_BLOCK_SOS_BACKEND_VMID;
    session->shm_vaddr = 0;
    session->shm_size = 0;
    session->shm_vmo = ZX_HANDLE_INVALID;
    session->event[0] = ZX_HANDLE_INVALID;
    session->event[1] = ZX_HANDLE_INVALID;

    if (was_in_use) {
        UFS_LOGI("rpmb session reset: vmid=%u\n", vmid);
    }
}

static int ufs_virtio_rpmb_process_request(ufs_virtio_rpmb_session_t* session,
                                           const ufs_virtio_rpmb_vq_req_t* req) {
    ufs_virtio_rpmb_packet_t* packet = ufs_virtio_rpmb_packet(session, req->slot_id);
    u16 msg_type = 0;
    int ret;

    if (packet == NULL) {
        UFS_LOGE("rpmb request dropped: vmid=%u slot=%u invalid packet slot\n", session->vmid,
                 req->slot_id);
        return -1;
    }

    msg_type = be16toh(packet->req_frame[0].req_resp);
    UFS_LOGI("rpmb request begin: vmid=%u slot=%u region=%u type=0x%x(%s) req_nfrm=%u rsp_nfrm=%u\n",
             session->vmid, req->slot_id, req->region, msg_type,
             ufs_virtio_rpmb_req_resp_to_str(msg_type), req->req_nfrm, req->rsp_nfrm);

    if (packet->magic != UFS_VIRTIO_RPMB_PACKET_MAGIC || packet->req_nfrm == 0 ||
        packet->rsp_nfrm == 0 || packet->req_nfrm > UFS_VIRTIO_RPMB_MAX_FRAMES ||
        packet->rsp_nfrm > UFS_VIRTIO_RPMB_MAX_FRAMES || packet->region > RPMB_REGION_3) {
        UFS_LOGW("rpmb request invalid packet: vmid=%u slot=%u magic=0x%x region=%u req_nfrm=%u rsp_nfrm=%u\n",
                 session->vmid, req->slot_id, packet->magic, packet->region, packet->req_nfrm,
                 packet->rsp_nfrm);
        packet->status = ZX_ERR_INVALID_ARGS;
        packet->rpmb_ret = -UFS_RPMB_DRIVER_ERROR;
        packet->rpmb_result = 0;
        packet->rsp_bytes = 0;
        return 0;
    }

    if (req->region != packet->region || req->req_nfrm != packet->req_nfrm ||
        req->rsp_nfrm != packet->rsp_nfrm) {
        UFS_LOGW("rpmb request mismatch: vmid=%u slot=%u req_region=%u pkt_region=%u req_nfrm=%u/%u rsp_nfrm=%u/%u\n",
                 session->vmid, req->slot_id, req->region, packet->region, req->req_nfrm,
                 packet->req_nfrm, req->rsp_nfrm, packet->rsp_nfrm);
        packet->status = ZX_ERR_INVALID_ARGS;
        packet->rpmb_ret = -UFS_RPMB_DRIVER_ERROR;
        packet->rpmb_result = 0;
        packet->rsp_bytes = 0;
        return 0;
    }

    ret = ufs_rpmb_request((u8)packet->region, (const struct rpmb_data_frame*)packet->req_frame,
                           packet->req_nfrm, (struct rpmb_data_frame*)packet->rsp_frame,
                           packet->rsp_nfrm, &packet->rpmb_result);
    packet->rpmb_ret = ret;
    packet->rsp_bytes = packet->rsp_nfrm * sizeof(ufs_virtio_rpmb_frame_t);
    packet->status = (ret < 0) ? ufs_virtio_rpmb_ret_to_status(ret) : ZX_OK;
    UFS_LOGI("rpmb request done: vmid=%u slot=%u region=%u type=0x%x(%s) status=%d rpmb_ret=%d result=%d rsp_bytes=%u\n",
             session->vmid, req->slot_id, packet->region, msg_type,
             ufs_virtio_rpmb_req_resp_to_str(msg_type), packet->status, packet->rpmb_ret,
             packet->rpmb_result, packet->rsp_bytes);

    return 0;
}

static int ufs_virtio_rpmb_worker(void* arg) {
    ufs_virtio_rpmb_session_t* session = (ufs_virtio_rpmb_session_t*)arg;
    ufs_virtio_rpmb_backend_t* backend = &g_ufs_virtio_rpmb_backend;
#if PERF_BIND_CPU_TEMPORARILY
    uint32_t cpu_mask = 0x20;

    _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_CUR_THREAD_AFFINITY, (void*)&cpu_mask);
#endif
    zx_thread_set_priority(UFS_VIRTIO_RPMB_THREAD_PRIORITY);
    UFS_LOGI("rpmb worker start: vmid=%u\n", session->vmid);

    for (;;) {
        zx_signals_t observed = 0;
        zx_status_t status;

        status = zx_object_wait_one(session->event[1], ZX_EVENT_SIGNALED,
                                    ZX_TIME_INFINITE, &observed);
        if (status != ZX_OK) {
            break;
        }

        zx_object_signal(session->event[1], ZX_EVENT_SIGNALED, 0);

        if (session->stop_worker) {
            break;
        }

        for (;;) {
            ufs_virtio_rpmb_vq_req_t req;
            uint16_t head;

            if (session->stop_worker) {
                break;
            }

            mtx_lock(&backend->lock);
            if (backend->suspended) {
                mtx_unlock(&backend->lock);
                break;
            }
            if (virtq_backend_get_avail(&session->vq, &req, &head) != 0) {
                mtx_unlock(&backend->lock);
                break;
            }

            backend->inflight_reqs++;
            mtx_unlock(&backend->lock);

            ufs_virtio_rpmb_process_request(session, &req);

            mtx_lock(&backend->lock);
            if (backend->inflight_reqs > 0) {
                backend->inflight_reqs--;
            }
            mtx_unlock(&backend->lock);
            virtq_backend_to_used(&session->vq, head);
        }

        if (session->stop_worker) {
            break;
        }
    }

    session->worker_running = false;
    UFS_LOGI("rpmb worker exit: vmid=%u stop=%d\n", session->vmid, session->stop_worker);
    return 0;
}

static zx_status_t ufs_virtio_rpmb_session_start(ufs_virtio_rpmb_session_t* session, u16 vmid) {
    zx_status_t status;
    int ret;

    if (session->in_use) {
        UFS_LOGI("rpmb session already running: vmid=%u\n", session->vmid);
        return ZX_OK;
    }

    UFS_LOGI("rpmb session start begin: vmid=%u\n", vmid);

    session->shm_size = ufs_virtio_rpmb_shm_bytes();
    status = zx_vmo_create(session->shm_size, 0, &session->shm_vmo);
    if (status != ZX_OK) {
        return status;
    }

    status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, 0,
                         session->shm_vmo, 0, session->shm_size, &session->shm_vaddr);
    if (status != ZX_OK) {
        ufs_virtio_rpmb_session_reset(session);
        return status;
    }

    memset((void*)session->shm_vaddr, 0, session->shm_size);

    status = zx_eventpair_create(0, &session->event[0], &session->event[1]);
    if (status != ZX_OK) {
        ufs_virtio_rpmb_session_reset(session);
        return status;
    }

    ret = vq_backend_init(&session->vq, (void*)session->shm_vaddr, session->shm_size,
                          UFS_VIRTIO_RPMB_QUEUE_DEPTH, session->event[1]);
    if (ret != 0) {
        ufs_virtio_rpmb_session_reset(session);
        return ZX_ERR_INTERNAL;
    }

    session->vmid = vmid;
    session->in_use = true;
    session->stop_worker = false;

    ret = thrd_create_with_name(&session->worker, ufs_virtio_rpmb_worker, session,
                                "ufs-rpmb-vq");
    if (ret != thrd_success) {
        ufs_virtio_rpmb_session_reset(session);
        return thrd_status_to_zx_status(ret);
    }

    session->worker_running = true;
    UFS_LOGI("rpmb session start done: vmid=%u shm_size=%zu\n", vmid, session->shm_size);
    return ZX_OK;
}

static zx_status_t ufs_virtio_rpmb_get_handle(ufs_virtio_rpmb_session_t* session, u8 handle_id,
                                              zx_handle_t* out_handle) {
    zx_status_t status;

    UFS_LOGI("rpmb get handle: vmid=%u handle_id=0x%x shm_vmo=%u event0=%u event1=%u\n",
             session->vmid, handle_id, session->shm_vmo, session->event[0], session->event[1]);
    if (handle_id == UFS_VIRTIO_RPMB_SHM_HANDLE_ID) {
        status = zx_handle_duplicate(session->shm_vmo, ZX_RIGHT_SAME_RIGHTS, out_handle);
        UFS_LOGI("rpmb get handle shm result: status=%d out=%u\n", status,
                 status == ZX_OK ? *out_handle : ZX_HANDLE_INVALID);
        return status;
    }
    if (handle_id == UFS_VIRTIO_RPMB_EVENT_HANDLE_ID) {
        status = zx_handle_duplicate(session->event[0], ZX_RIGHT_SAME_RIGHTS, out_handle);
        UFS_LOGI("rpmb get handle event result: status=%d out=%u\n", status,
                 status == ZX_OK ? *out_handle : ZX_HANDLE_INVALID);
        return status;
    }

    return ZX_ERR_INVALID_ARGS;
}

static void ufs_virtio_rpmb_stop_session(ufs_virtio_rpmb_session_t* session) {
    if (session == NULL || !session->in_use) {
        return;
    }

    UFS_LOGI("rpmb session stop begin: vmid=%u\n", session->vmid);
    session->stop_worker = true;
    if (session->event[1] != ZX_HANDLE_INVALID) {
        zx_object_signal(session->event[1], 0, ZX_EVENT_SIGNALED);
    }
    if (session->worker_running) {
        thrd_join(session->worker, NULL);
    }
    ufs_virtio_rpmb_session_reset(session);
}

static ufs_virtio_rpmb_session_t* ufs_virtio_rpmb_find_session(u16 vmid) {
    ufs_virtio_rpmb_backend_t* backend = &g_ufs_virtio_rpmb_backend;
    uint32_t index;

    for (index = 0; index < UFS_VIRTIO_RPMB_MAX_GUESTS; ++index) {
        if (backend->sessions[index].in_use && backend->sessions[index].vmid == vmid) {
            return &backend->sessions[index];
        }
    }

    return NULL;
}

static ufs_virtio_rpmb_session_t* ufs_virtio_rpmb_alloc_session(void) {
    ufs_virtio_rpmb_backend_t* backend = &g_ufs_virtio_rpmb_backend;
    uint32_t index;

    for (index = 0; index < UFS_VIRTIO_RPMB_MAX_GUESTS; ++index) {
        if (!backend->sessions[index].in_use) {
            return &backend->sessions[index];
        }
    }

    return NULL;
}

zx_status_t ufs_virtio_rpmb_ioctl(mtk_ufs_t* ufs, uint32_t op, const void* in_buf, size_t in_len,
                                  void* out_buf, size_t out_len, size_t* out_actual) {
    ufs_virtio_rpmb_backend_t* backend = &g_ufs_virtio_rpmb_backend;
    zx_status_t status;
    ufs_virtio_rpmb_session_t* session;

    (void)ufs;
    ufs_virtio_rpmb_backend_init_once();

    if (out_actual != NULL) {
        *out_actual = 0;
    }

    mtx_lock(&backend->lock);

    switch (op) {
    case GRT_VRPMB_IOCTL_START: {
        const ufs_virtio_rpmb_start_param_t* start = in_buf;

        UFS_LOGI("rpmb ioctl start: op=0x%x\n", op);

        if (in_buf == NULL || in_len != sizeof(*start) || start->vmid >= UFS_VIRTIO_RPMB_MAX_GUESTS) {
            status = ZX_ERR_INVALID_ARGS;
            break;
        }

        session = ufs_virtio_rpmb_find_session(start->vmid);
        if (session == NULL) {
            session = ufs_virtio_rpmb_alloc_session();
        }
        if (session == NULL) {
            status = ZX_ERR_NO_RESOURCES;
            break;
        }

        status = ufs_virtio_rpmb_session_start(session, start->vmid);
        break;
    }
    case GRT_VRPMB_IOCTL_GET_HANDLE: {
        const ufs_virtio_rpmb_get_handle_param_t* param = in_buf;

        UFS_LOGI("rpmb ioctl get-handle: op=0x%x\n", op);

        if (in_buf == NULL || out_buf == NULL || in_len != sizeof(*param) ||
            out_len < sizeof(zx_handle_t)) {
            status = ZX_ERR_INVALID_ARGS;
            break;
        }

        session = ufs_virtio_rpmb_find_session(param->vmid);
        if (session == NULL) {
            status = ZX_ERR_NOT_FOUND;
            break;
        }

        status = ufs_virtio_rpmb_get_handle(session, param->handle_id, out_buf);
        if (status == ZX_OK && out_actual != NULL) {
            *out_actual = sizeof(zx_handle_t);
        }
        break;
    }
    case GRT_VRPMB_IOCTL_STOP:
        UFS_LOGI("rpmb ioctl stop: op=0x%x\n", op);
        if (in_buf == NULL || in_len != sizeof(ufs_virtio_rpmb_start_param_t)) {
            status = ZX_ERR_INVALID_ARGS;
            break;
        }
        session = ufs_virtio_rpmb_find_session(((const ufs_virtio_rpmb_start_param_t*)in_buf)->vmid);
        if (session == NULL) {
            status = ZX_ERR_NOT_FOUND;
            break;
        }
        ufs_virtio_rpmb_stop_session(session);
        status = ZX_OK;
        break;
    default:
        status = ZX_ERR_NOT_SUPPORTED;
        break;
    }

    mtx_unlock(&backend->lock);
    UFS_LOGI("rpmb ioctl done: op=0x%x status=%d\n", op, status);
    return status;
}

zx_status_t ufs_virtio_rpmb_suspend(mtk_ufs_t* ufs, uint32_t flags) {
    ufs_virtio_rpmb_backend_t* backend = &g_ufs_virtio_rpmb_backend;
    uint32_t wait_ms = 0;

    (void)ufs;
    ufs_virtio_rpmb_backend_init_once();
    UFS_LOGI("rpmb backend suspend flags=0x%x\n", flags);

    mtx_lock(&backend->lock);
    backend->suspended = true;
    while (backend->inflight_reqs > 0) {
        mtx_unlock(&backend->lock);
        zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
        wait_ms++;
        mtx_lock(&backend->lock);
    }
    mtx_unlock(&backend->lock);

    UFS_LOGI("rpmb backend suspend done wait_ms=%u\n", wait_ms);
    return ZX_OK;
}

zx_status_t ufs_virtio_rpmb_resume(mtk_ufs_t* ufs, uint32_t flags) {
    ufs_virtio_rpmb_backend_t* backend = &g_ufs_virtio_rpmb_backend;
    uint32_t index;

    (void)ufs;
    ufs_virtio_rpmb_backend_init_once();
    UFS_LOGI("rpmb backend resume flags=0x%x\n", flags);

    mtx_lock(&backend->lock);
    backend->suspended = false;
    for (index = 0; index < UFS_VIRTIO_RPMB_MAX_GUESTS; ++index) {
        ufs_virtio_rpmb_session_t* session = &backend->sessions[index];

        if (!session->in_use || session->event[1] == ZX_HANDLE_INVALID) {
            continue;
        }

        zx_object_signal(session->event[1], 0, ZX_EVENT_SIGNALED);
    }
    mtx_unlock(&backend->lock);

    UFS_LOGI("rpmb backend resume done\n");
    return ZX_OK;
}

void ufs_virtio_rpmb_release(mtk_ufs_t* ufs) {
    ufs_virtio_rpmb_backend_t* backend = &g_ufs_virtio_rpmb_backend;
    uint32_t index;

    (void)ufs;
    if (!backend->initialized) {
        return;
    }

    UFS_LOGI("rpmb backend release begin\n");
    mtx_lock(&backend->lock);
    for (index = 0; index < UFS_VIRTIO_RPMB_MAX_GUESTS; ++index) {
        ufs_virtio_rpmb_session_t* session = &backend->sessions[index];

        if (!session->in_use) {
            continue;
        }

        ufs_virtio_rpmb_stop_session(session);
    }
    mtx_unlock(&backend->lock);
    UFS_LOGI("rpmb backend release done\n");
}
