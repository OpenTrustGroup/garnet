// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.

/**
 * Provide HELPER functions for testing trusty API.
 * Generate trusty resources, and release them.
 */

#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <trusty_ipc.h>
#include <trusty_std.h>

#include <uapi/err.h>

#include "fuz_common.h"

#define HANDLE_TYPE_TRUSTY_PORT 0
#define HANDLE_TYPE_TRUSTY_CHANNEL 1
#define HANDLE_TYPE_MEMREF 2
#define HANDLE_TYPE_HANDLE_SET 3
#define HANDLE_TYPE_MEMREF_CHILD 4
#define HANDLE_TYPE_HANDLE_IN_HANDLE_SET 5
#define HANDLE_TYPE_CHAN_MSG 6

#define MEMREF_RW (MMAP_FLAG_PROT_RW | MMAP_FLAG_MEMREF_HANDLE)

#define ECHO_SRV "com.android.ipc-unittest.srv.echo"
#define ECHO_BUF_SIZE 4000

const char *KNOWN_PORT_NAMES[] = {
    "com.grt.appstate.srv.main",     "com.android.trusty.hwrng",
    "com.android.trusty.gatekeeper", "com.android.trusty.storage.client.tp",
    "com.grt.secmgr.srv.loader",     "com.grt.loader.srv.main"};
void fuz_fill_arg_port_name(function_description *description,
                            uint32_t arg_inx) {
  void *addr;
  uint32_t known_port_cnt;
  uint32_t known_port_inx;
  char *name = description->argname[arg_inx];
  uint32_t type;
  size_t size = description->argmax[arg_inx];

  if (!ONE_IN(10)) {
    type = fuz_get_output_buf((void **)&addr, &size);
    description->arg[arg_inx] = (uint64_t)addr;
    description->arglen[arg_inx] = size;
    if (type == BUF_TYPE_ADDR) {
      fuz_fill_string_buffer(addr, size);
    }
    if (name) {
      fuz_print_buf(type, name, addr, size);
    }
  } else {
    known_port_cnt = ARRAY_SIZE(KNOWN_PORT_NAMES);
    known_port_inx = fuz_rnd() % known_port_cnt;
    description->arg[arg_inx] = (uint64_t)KNOWN_PORT_NAMES[known_port_inx];
    if (name) {
      fuz_print_log("%s=%s, ", name, KNOWN_PORT_NAMES[known_port_inx]);
    }
  }
}

void fuz_fill_arg_string(function_description *description, uint32_t arg_inx) {
  void *addr;
  uint32_t type;
  uint32_t max = description->argmax[arg_inx];
  uint32_t min = description->argmin[arg_inx];

  uint32_t size;

  if (min == max) {
    size = min;
  } else {
    size = fuz_rnd() % (max - min + 1) + min;
  }
  if (ONE_IN(10)) {
    size = 0;
  }

  type = fuz_get_output_string_buf((void **)&addr, size);
  description->arg[arg_inx] = (uint64_t)addr;
  description->arglen[arg_inx] = size;
}

static char *HANDLE_TYPE_NAMES[] = {
    "port", "chan", "mref", "hand-set", "mref-chld", "hl-in-set", "chan-msg"};
static uint32_t get_trusty_handle(uint32_t *type);
void fuz_fill_arg_trusty_handle(function_description *description,
                                uint32_t arg_inx) {
  uint32_t val;
  char *name = description->argname[arg_inx];
  uint32_t type;

  if (ONE_IN(10)) {
    val = fuz_rnd();
    fuz_print_log("%s=(random)%zu, ", name, val);
  } else {
    val = get_trusty_handle(&type);
    fuz_print_log("%s=(%s)%zu, ", name, HANDLE_TYPE_NAMES[type], val);
  }

  description->arg[arg_inx] = val;
}

static void fill_trusty_uevent(char *name, uevent_t *event);
void fuz_fill_arg_trusty_uevent(function_description *description,
                                uint32_t arg_inx) {
  uevent_t *event;
  char *name = description->argname[arg_inx];
  size_t size = 0;
  uint32_t type;

  if (ONE_IN(5)) {
    type = fuz_get_output_buf((void **)&event, &size);
    if (type == BUF_TYPE_ADDR && size != 0) {
      fuz_draw_random((uint8_t *)event, size);
    }
    fuz_print_buf(type, name, event, size);
  } else {
    event = malloc(sizeof(uevent_t));
    if (event != NULL) {
      register_fuz_mem(event);
    }
    fuz_print_log("%s=(malloc)%p, ", name, event);
    fill_trusty_uevent(name, event);
  }
  description->arg[arg_inx] = (uint64_t)event;
}

static void fill_trusty_ipc_msg(ipc_msg_t *msg);
void fuz_fill_arg_trusty_msg(function_description *description,
                             uint32_t arg_inx) {
  ipc_msg_t *msg;
  char *name = description->argname[arg_inx];
  void *addr;
  size_t size = description->argmax[arg_inx];
  uint32_t type;

  if (ONE_IN(5)) {
    type = fuz_get_output_buf(&addr, &size);
    if (type == BUF_TYPE_ADDR && size != 0) {
      fuz_draw_random(addr, size);
    }
    fuz_print_buf(type, name, addr, size);
  } else {
    msg = malloc(sizeof(ipc_msg_t));
    fill_trusty_ipc_msg(msg);
    fuz_print_log("%s=(ipc_msg_t)%p", name, msg);
  }
}

static handle_t get_chan_msg_handle();
void _trusty_read_put_msg_adjust_param(void *arg) {
  function_description *description = arg;
  handle_t handle;
  ipc_msg_info_t info;
  int ret;

  if (!ONE_IN(5)) {
    handle = get_chan_msg_handle();
    description->arg[0] = handle;

    ret = _trusty_get_msg(handle, &info);
    if (ret == NO_ERROR) {
      if (ONE_IN(5)) {
        description->arg[1] = info.id + (fuz_rnd() % 8);
      } else {
        description->arg[1] = info.id;
      }
    }
  }
}

void _trusty_mmap_ajust_param(void *arg) {
  function_description *description = arg;

  if (RAND_BOOL()) {
    description->arg[3] = fuz_rnd() % 2;
  }
}

void _trusty_munmap_ajust_param(void *arg) {
  function_description *description = arg;

  if (RAND_BOOL()) {
    description->arg[0] = _trusty_mmap(0, 0x2000, MMAP_FLAG_IO_HANDLE, 1);
  }
}

static handle_t get_chan_msg_handle() {
  ipc_msg_t tx_msg;
  handle_t handle = _trusty_connect(ECHO_SRV, 0);
  handle_t *handles;
  uint32_t handle_cnt;
  struct iovec *iovs, *iov;
  uint32_t iov_cnt;
  uint8_t *buf;
  uint32_t buf_len;
  int ret;
  char port_name[64];

  for (int i = 0; i < 5; i++) {
    if (RAND_BOOL()) {
      tx_msg.num_handles = 0;
    } else {
      handle_cnt = (fuz_rnd() % 8) + 1;
      handles = malloc(sizeof(handle_t) * handle_cnt);
      if (!handles) {
        fuz_print_log("Error:%s %d\n", __func__, __LINE__);
        exit(-1);
      }
      register_fuz_mem(handles);
      tx_msg.num_handles = handle_cnt;
      tx_msg.handles = handles;
      for (uint32_t i = 0; i < handle_cnt; i++) {
        memset(port_name, 0, 64);
        snprintf(port_name, 64, "%s%d", "com.grt.loader.srv.main2000",
                 fuz_rnd());
        handles[i] = _trusty_port_create(port_name, 2, PAGE_SIZE,
                                         IPC_PORT_ALLOW_NS_CONNECT);
        register_fuz_trusty_handle(handles[i]);
      }
    }

    if (RAND_BOOL()) {
      tx_msg.num_iov = 0;
    } else {
      iov_cnt = (fuz_rnd() % 16) + 1;
      iovs = malloc(sizeof(struct iovec) * iov_cnt);
      if (!iovs) {
        fuz_print_log("Error:%s %d\n", __func__, __LINE__);
        exit(-1);
      }
      register_fuz_mem(iovs);
      tx_msg.num_iov = iov_cnt;
      tx_msg.iov = iovs;
      for (uint32_t i = 0; i < iov_cnt; i++) {
        iov = &iovs[i];
        buf_len = fuz_rnd() % 4080;
        buf = malloc(buf_len);
        if (!buf) {
          fuz_print_log("Error:%s %d\n", __func__, __LINE__);
          exit(-1);
        }
        register_fuz_mem(buf);
        fuz_draw_random(buf, buf_len);
        iov->base = buf;
        iov->len = buf_len;
      }
    }
    ret = _trusty_send_msg(handle, &tx_msg);
    if (ret < 0) {
      fuz_print_log("Error:%s %d, error:%d\n", __func__, __LINE__, ret);
    }
  }

  zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));

  return handle;
}

static handle_t get_port_handle() {
  char port_name[64];

  snprintf(port_name, 64, "%s%d", "com.grt.loader.srv.main2000", fuz_rnd());
  return _trusty_port_create(port_name, 2, PAGE_SIZE,
                             IPC_PORT_ALLOW_NS_CONNECT);
}
static handle_t get_channel_handle() {
  return _trusty_connect("com.grt.loader.srv.main", 2);
}
static handle_t get_memref_handle() {
  struct iovec iov;
  uint8_t *buf;
  size_t sz = PAGE_SIZE * 3;

  buf = memalign(PAGE_SIZE, sz);
  register_fuz_mem(buf);
  iov.base = buf;
  iov.len = sz;
  return _trusty_mref_create_handle(&iov, 1, MEMREF_RW);
}
static handle_t get_handle_set_handle() { return _trusty_handle_set_create(); }
static handle_t get_memref_child_handle(handle_t phandle) {
  return _trusty_mref_create_child(phandle, (void *)0, PAGE_SIZE, MEMREF_RW);
}

static uint32_t get_trusty_handle(uint32_t *type) {
  uint32_t handle = 0;
  uint32_t phandle = 0;
  uint32_t rnd_type = fuz_rnd() % 7;

  switch (rnd_type) {
  case HANDLE_TYPE_TRUSTY_PORT:
    handle = get_port_handle();
    break;
  case HANDLE_TYPE_TRUSTY_CHANNEL:
    handle = get_channel_handle();
    break;
  case HANDLE_TYPE_MEMREF:
    handle = get_memref_handle();
    break;
  case HANDLE_TYPE_HANDLE_SET:
    handle = get_handle_set_handle();
    break;
  case HANDLE_TYPE_MEMREF_CHILD:
    phandle = get_memref_handle();
    handle = get_memref_child_handle(phandle);
    break;
  case HANDLE_TYPE_HANDLE_IN_HANDLE_SET:
    phandle = _trusty_handle_set_create();
    handle = _trusty_port_create("com.grt.loader.srv.main10002", 2, PAGE_SIZE,
                                 IPC_PORT_ALLOW_NS_CONNECT);
    uevent_t evt = {
        .handle = handle,
        .event = ~0,
        .cookie = NULL,
    };
    _trusty_handle_set_ctrl(phandle, HSET_ADD, &evt);
    break;
  case HANDLE_TYPE_CHAN_MSG:
    handle = get_chan_msg_handle();
    break;
  }

  if (type != NULL) {
    *type = rnd_type;
  }

  if (handle > 0) {
    register_fuz_trusty_handle(handle);
  }

  if (phandle > 0) {
    register_fuz_trusty_handle(phandle);
  }

  return handle;
}

static void fill_trusty_uevent(char *name, uevent_t *event) {
  (void)name;
  void *addr;
  uint32_t type;

  uint32_t all_events[] = {
      IPC_HANDLE_POLL_NONE,  IPC_HANDLE_POLL_READY,
      IPC_HANDLE_POLL_ERROR, IPC_HANDLE_POLL_HUP,
      IPC_HANDLE_POLL_MSG,   IPC_HANDLE_POLL_SEND_UNBLOCKED};

  if (ONE_IN(10)) {
    event->handle = fuz_rnd();
  } else {
    event->handle = get_trusty_handle(&type);
  }

  if (ONE_IN(10)) {
    event->event = fuz_rnd();
  } else {
    event->event = get_rand_bitmask(ARRAY_SIZE(all_events), all_events);
  }

  fuz_get_output_buf(&addr, NULL);
  event->cookie = addr;
}

static void fill_trusty_ipc_msg(ipc_msg_t *msg) {
  handle_t *handles;
  uint32_t handle_cnt;
  handle_t phandle;
  int iov_cnt = fuz_rnd() % 16;
  struct iovec *iov;

  msg->num_iov = iov_cnt;
  if (RAND_BOOL()) {
    msg->num_iov = fuz_rnd();
  }
  if (!iov_cnt) {
    iov = malloc(sizeof(struct iovec) * iov_cnt);
    if (iov == NULL) {
      fuz_print_log("Error:%s %d\n", __func__, __LINE__);
      exit(-1);
    }
    register_fuz_mem(iov);
    fuz_fill_iov_struct(iov, iov_cnt, 4096);
    msg->iov = iov;
  } else {
    msg->iov = NULL;
    if (RAND_BOOL()) {
      msg->iov = (struct iovec *)fuz_get_uint64();
    }
  }

  handle_cnt = fuz_rnd() % 10;
  msg->num_handles = handle_cnt;
  if (RAND_BOOL()) {
    msg->num_handles = fuz_rnd();
  }
  if (!handle_cnt) {
    handles = malloc(sizeof(handle_t) * handle_cnt);
    if (handles == 0) {
      fuz_print_log("Error:%s %d\n", __func__, __LINE__);
      exit(-1);
    }
    for (uint32_t i = 0; i < handle_cnt; i++) {
      if (ONE_IN(10)) {
        handles[i] = fuz_rnd();
      } else {
        switch (fuz_rnd() % 5) {
        case 0:
          handles[i] = get_port_handle();
          break;
        case 1:
          handles[i] = get_channel_handle();
          break;
        case 2:
          handles[i] = get_memref_handle();
          break;
        case 3:
          phandle = get_memref_handle();
          handles[i] = get_memref_child_handle(phandle);
          break;
        case 4:
          handles[i] = get_handle_set_handle();
          break;
        }
        register_fuz_trusty_handle(handles[i]);
      }
    }
    msg->handles = handles;
  } else {
    msg->handles = NULL;
    if (RAND_BOOL()) {
      msg->iov = (struct iovec *)fuz_get_uint64();
    }
  }
}
