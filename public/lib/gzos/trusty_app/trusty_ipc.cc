#include "garnet/public/lib/gzos/trusty_app/trusty_uuid.h"
#include "garnet/public/lib/gzos/trusty_app/trusty_ipc.h"

extern "C" {

long nanosleep(uint32_t clock_id, uint32_t flags, uint64_t sleep_time) {
  return 0;
}

long port_create(const char *path, uint32_t num_recv_bufs,
                 uint32_t recv_buf_size, uint32_t flags) {
  return 0;
}

long connect(const char *path, uint32_t flags) {
  return 0;
}

long accept(uint32_t handle_id, uuid_t *peer_uuid) {
  return 0;
}

long close(uint32_t handle_id) {
  return 0;
}

long set_cookie(uint32_t handle, void *cookie) {
  return 0;
}

long handle_set_create(void) {
  return 0;
}

long handle_set_ctrl(uint32_t handle, uint32_t cmd, struct uevent *evt) {
  return 0;
}

long wait(uint32_t handle_id, uevent_t *event, uint32_t timeout_msecs) {
  return 0;
}

long wait_any(uevent_t *event, uint32_t timeout_msecs) {
  return 0;
}

long get_msg(uint32_t handle, ipc_msg_info_t *msg_info) {
  return 0;
}

long read_msg(uint32_t handle, uint32_t msg_id, uint32_t offset,
              ipc_msg_t *msg) {
  return 0;
}

long put_msg(uint32_t handle, uint32_t msg_id) {
  return 0;
}

long send_msg(uint32_t handle, ipc_msg_t *msg) {
  return 0;
}

}
