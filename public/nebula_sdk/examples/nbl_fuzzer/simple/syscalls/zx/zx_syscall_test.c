#include <uapi/err.h>

#include <fuz_common.h>
#include <zircon/nslist.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/policy.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

void zx_thread_set_priority_fun(int32_t prio) {
  zx_handle_t thread;
  zx_thread_create(zx_process_self(), "", 0, 0, &thread);
  zx_thread_set_priority(prio);
  zx_handle_close(thread);
}

void zx_port_wait_fun(zx_handle_t handle, zx_time_t deadline,
                      zx_port_packet_t *packet, size_t count) {
  zx_handle_t thread;
  zx_thread_create(zx_process_self(), "", 0, 0, &thread);
  zx_port_wait(handle, deadline, packet, count);
  zx_handle_close(thread);
}

void zx_object_wait_many_fun(zx_wait_item_t *items, uint32_t count,
                             zx_time_t deadline) {
  zx_object_wait_many(items, count, deadline);
  exit(0);
}

void zx_vmar_map_fun(zx_handle_t vmar_handle, uint32_t map_flags,
                     size_t vmar_offset, zx_handle_t vmo_handle,
                     uint64_t vmo_offset, size_t len) {
  uintptr_t region_addr;
  zx_vmar_map(vmar_handle, map_flags, vmar_offset, vmo_handle, vmo_offset, len,
              &region_addr);
}

void zx_thread_read_state_fun(zx_handle_t handle, uint32_t kind, void *buffer,
                              size_t len) {
  register_fuz_zx_handle(handle);
  zx_thread_start(handle, 1, 1, 1, 1);
  zx_thread_read_state(handle, kind, buffer, len);
  zx_task_kill(handle);
}

void zx_thread_write_state_fun(zx_handle_t handle, uint32_t kind,
                               const void *buffer, size_t buffer_len) {
  register_fuz_zx_handle(handle);
  zx_thread_start(handle, 1, 1, 1, 1);
  zx_thread_write_state(handle, kind, buffer, buffer_len);
  zx_task_kill(handle);
}
