// Copyright 2019 GoldenRiver Technology Co., Ltd. All rights reserved.
#pragma once

#include <stddef.h>
#include <zircon/nslist.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

void zx_thread_set_priority_fun(int32_t prio);

void zx_port_wait_fun(zx_handle_t handle, zx_time_t deadline,
                      zx_port_packet_t *packet, size_t count);

void zx_object_wait_many_fun(zx_wait_item_t *items, uint32_t count,
                             zx_time_t deadline);

void zx_vmar_map_fun(zx_handle_t vmar_handle, uint32_t map_flags,
                     size_t vmar_offset, zx_handle_t vmo_handle,
                     uint64_t vmo_offset, size_t len);

void zx_thread_read_state_fun(zx_handle_t handle, uint32_t kind, void *buffer,
                              size_t len);

void zx_thread_write_state_fun(zx_handle_t handle, uint32_t kind,
                               const void *buffer, size_t buffer_len);
