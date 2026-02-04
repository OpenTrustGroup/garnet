// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <fuz_common.h>

#include <trusty_std.h>
#include <uapi/err.h>

#include "service_utils.h"

int sync_connect(const char *port_name, uint64_t timeout) {
  int rc;
  uevent_t evt;
  handle_t chan;

  rc =
      _trusty_connect(port_name, IPC_CONNECT_ASYNC | IPC_CONNECT_WAIT_FOR_PORT);
  if (rc >= 0) {
    chan = (handle_t)rc;
    rc = _trusty_wait(chan, &evt, timeout);
    if (rc == 0) {
      rc = ERR_BAD_STATE;
      if (evt.handle == chan) {
        if (evt.event & IPC_HANDLE_POLL_READY)
          return chan;

        if (evt.event & IPC_HANDLE_POLL_HUP)
          rc = ERR_CHANNEL_CLOSED;
      }
    }
    _trusty_close(chan);
  }
  return rc;
}

int send_msg_to_service(const char *port_name, void *buf, size_t size,
                        bool wait_reply, uint32_t read_len) {
  int rc;
  handle_t chan;
  ipc_msg_t tx_msg;
  iovec_t tx_iov;
  uint8_t data[4096];
  uint32_t receive_len = 0;

  rc = sync_connect(port_name, 1000);
  if (rc < NO_ERROR) {
    fuz_print_log("Error:%s %d, rc:%d\n", __func__, __LINE__, rc);
    return rc;
  }

  chan = rc;
  register_fuz_trusty_handle(chan);

  tx_iov.base = buf;
  tx_iov.len = size;
  tx_msg.num_iov = 1;
  tx_msg.iov = &tx_iov;
  tx_msg.num_handles = 0;
  tx_msg.handles = NULL;
  rc = _trusty_send_msg(chan, &tx_msg);
  if (rc < NO_ERROR) {
    fuz_print_log("Error:%s %d, rc:%d\n", __func__, __LINE__, rc);
    return rc;
  }

  if (wait_reply) {
    iovec_t rx_iov = {
        .base = data,
        .len = 4096,
    };
    ipc_msg_t rx_msg = {
        .iov = &rx_iov,
        .num_iov = 1,
    };

    do {
      if (!wait_reply) {
        break;
      }

      uevent_t uevt;
      rc = _trusty_wait(chan, &uevt, -1);
      if (rc != NO_ERROR) {
        return rc;
      }

      ipc_msg_info_t inf;
      rc = _trusty_get_msg(chan, &inf);
      if (rc != NO_ERROR) {
        return rc;
      }

      if (inf.len > rx_msg.iov[0].len) {
        // received too much data
        rc = ERR_BAD_LEN;
        return rc;
      }

      rc = _trusty_read_msg(chan, inf.id, 0, &rx_msg);
      if (rc < 0) {
        return rc;
      }

      receive_len += (size_t)rc;
      _trusty_put_msg(chan, inf.id);
    } while (receive_len < read_len);
  }

  return NO_ERROR;
}

void send_random_msg(const char *port_name) {
  void *addr;
  uint32_t size;

  generate_random_msg(&addr, &size);
  send_msg_to_service(port_name, addr, size, false, 0);
  return;
}

void generate_random_msg(void **addr, uint32_t *size) {
  int res;

  *size = fuz_rnd() % 4097;
  *addr = malloc(*size);
  if (*addr == NULL) {
    fuz_print_log("Error: %s %d\n", __func__, __LINE__);
    exit(-1);
  } else {
    register_fuz_mem(*addr);
  }

  res = fuz_draw_random(*addr, *size);
  if (res != NO_ERROR) {
    fuz_print_log("Error: %s %d, err:%d\n", __func__, __LINE__, res);
    exit(-1);
  }
}
