/*
 * Copyright (c) 2012-2018 LK Trusty Authors. All Rights Reserved.
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

#pragma once

__BEGIN_CDECLS

long write(uint32_t fd, void *msg, uint32_t size);
long brk(uint32_t brk);
long exit_etc(int32_t status, uint32_t flags);
long read(uint32_t fd, void *msg, uint32_t size);
long ioctl(uint32_t fd, uint32_t req, void *buf);
long nanosleep(uint32_t clock_id, uint32_t flags, uint64_t sleep_time);
long gettime(uint32_t clock_id, uint32_t flags, int64_t *time);
long mmap(void *uaddr, uint32_t size, uint32_t flags, uint32_t handle);
long munmap(void *uaddr, uint32_t size);
long prepare_dma(void *uaddr, uint32_t size, uint32_t flags,
                 struct dma_pmem *pmem);
long finish_dma(void *uaddr, uint32_t size, uint32_t flags);
long port_create(const char *path, uint32_t num_recv_bufs,
                 uint32_t recv_buf_size, uint32_t flags);
long connect(const char *path, uint32_t flags);
long accept(uint32_t handle_id, uuid_t *peer_uuid);
long close(uint32_t handle_id);
long set_cookie(uint32_t handle, void *cookie);
long handle_set_create(void);
long handle_set_ctrl(uint32_t handle, uint32_t cmd, struct uevent *evt);
long wait(uint32_t handle_id, uevent_t *event, uint32_t timeout_msecs);
long wait_any(uevent_t *event, uint32_t timeout_msecs);
long get_msg(uint32_t handle, ipc_msg_info_t *msg_info);
long read_msg(uint32_t handle, uint32_t msg_id, uint32_t offset,
              ipc_msg_t *msg);
long put_msg(uint32_t handle, uint32_t msg_id);
long send_msg(uint32_t handle, ipc_msg_t *msg);

__END_CDECLS
