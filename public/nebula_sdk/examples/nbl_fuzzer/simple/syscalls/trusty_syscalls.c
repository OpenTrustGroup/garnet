// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <fuz_common.h>

#include <trusty_std.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <uapi/err.h>

// long _trusty_port_create (const char *path, uint32_t num_recv_bufs, uint32_t
// recv_buf_size, uint32_t flags);
static uint32_t PORT_FLAGS[] = {IPC_PORT_ALLOW_TA_CONNECT,
                                IPC_PORT_ALLOW_NS_CONNECT};
struct function_description _trusty_port_create_test = {
    .funcname = "_trusty_port_create",
    .funcaddr = _trusty_port_create,
    .argnum = 4,
    .argname = {"path", "num_recv_bufs", "recv_buf_size", "flags"},
    .argtype = {ARG_PORT_NAME, ARG_INT, ARG_INT, ARG_FLAGS},
    .argmax = {64, 32, 4096},
    .argselcnt[3] = ARRAY_SIZE(PORT_FLAGS),
    .argselect[3] = PORT_FLAGS,
};

PrepareTest(_trusty_port_create) { return &_trusty_port_create_test; }

// long _trusty_connect (const char *path, uint32_t flags);
static uint32_t PORT_CONNECT_FLAGS[] = {IPC_CONNECT_WAIT_FOR_PORT,
                                        IPC_CONNECT_ASYNC};
struct function_description _trusty_connect_test = {
    .funcname = "_trusty_connect",
    .funcaddr = _trusty_connect,
    .argnum = 2,
    .argname = {"path", "flags"},
    .argtype = {ARG_PORT_NAME, ARG_FLAGS},
    .argmax = {64},
    .argselcnt[1] = ARRAY_SIZE(PORT_CONNECT_FLAGS),
    .argselect[1] = PORT_CONNECT_FLAGS,
    .block = 1};
PrepareTest(_trusty_connect) { return &_trusty_connect_test; }

// long _trusty_accept (uint32_t handle_id, struct uuid *peer_uuid);
struct function_description _trusty_accept_test = {
    .funcname = "_trusty_accept",
    .funcaddr = _trusty_accept,
    .argnum = 2,
    .argname = {"handle_id", "peer_uuid"},
    .argtype = {ARG_TRUSTY_HANDLE, ARG_ADDRESS},
    .argmax[1] = sizeof(struct uuid)};
PrepareTest(_trusty_accept) { return &_trusty_accept_test; }

// long _trusty_set_cookie (uint32_t handle, void *cookie);
struct function_description _trusty_set_cookie_test = {
    .funcname = "_trusty_set_cookie",
    .funcaddr = _trusty_set_cookie,
    .argnum = 2,
    .argname = {"handle", "cookie"},
    .argtype = {ARG_TRUSTY_HANDLE, ARG_ADDRESS}};
PrepareTest(_trusty_set_cookie) { return &_trusty_set_cookie_test; }

// long _trusty_handle_set_create (void);
struct function_description _trusty_handle_set_create_test = {
    .funcname = "_trusty_handle_set_create",
    .funcaddr = _trusty_handle_set_create,
    .argnum = 0,
};
PrepareTest(_trusty_handle_set_create) {
  return &_trusty_handle_set_create_test;
}

// long _trusty_handle_set_ctrl (uint32_t handle, uint32_t cmd, struct uevent
// *evt);
uint32_t HANDLE_SET_CMDS[] = {HSET_ADD, HSET_DEL, HSET_MOD};
struct function_description _trusty_handle_set_ctrl_test = {
    .funcname = "_trusty_handle_set_ctrl",
    .funcaddr = _trusty_handle_set_ctrl,
    .argnum = 0,
    .argname = {"handle", "cmd", "evt"},
    .argtype = {ARG_TRUSTY_HANDLE, ARG_SELECT, ARG_TRUSTY_UEVENT},
    .argselcnt[1] = ARRAY_SIZE(HANDLE_SET_CMDS),
    .argselect[1] = HANDLE_SET_CMDS};
PrepareTest(_trusty_handle_set_ctrl) { return &_trusty_handle_set_ctrl_test; }

// long _trusty_close (uint32_t handle_id);
struct function_description _trusty_close_test = {
    .funcname = "_trusty_close",
    .funcaddr = _trusty_close,
    .argnum = 1,
    .argname = {"handle"},
    .argtype = {ARG_TRUSTY_HANDLE}};
PrepareTest(_trusty_close) { return &_trusty_close_test; }

// long _trusty_wait (uint32_t handle_id, struct uevent *event, uint64_t
// timeout_msecs);
struct function_description _trusty_wait_test = {
    .funcname = "_trusty_wait",
    .funcaddr = _trusty_wait,
    .argnum = 3,
    .argname = {"handle", "event", "timeout_msecs"},
    .argtype = {ARG_TRUSTY_HANDLE, ARG_TRUSTY_UEVENT, ARG_INT64},
    .block = 1};
PrepareTest(_trusty_wait) { return &_trusty_wait_test; }

// long _trusty_wait_any (struct uevent *event, uint64_t timeout_msecs);
struct function_description _trusty_wait_any_test = {
    .funcname = "_trusty_wait_any",
    .funcaddr = _trusty_wait_any,
    .argnum = 2,
    .argname = {"event", "timeout_msecs"},
    .argtype = {ARG_TRUSTY_UEVENT, ARG_INT64},
    .block = 1};
PrepareTest(_trusty_any_wait) { return &_trusty_wait_any_test; }

// long _trusty_get_msg (uint32_t handle, struct ipc_msg_info *msg_info);
struct function_description _trusty_get_msg_test = {
    .funcname = "_trusty_get_msg",
    .funcaddr = _trusty_get_msg,
    .argnum = 2,
    .argname = {"handle", "msg_info"},
    .argtype = {ARG_TRUSTY_HANDLE, ARG_ADDRESS},
    .argmax[1] = sizeof(struct ipc_msg_info),
};
PrepareTest(_trusty_get_msg) { return &_trusty_get_msg_test; }

// long _trusty_read_msg (uint32_t handle, uint32_t msg_id, uint32_t offset,
// struct ipc_msg *msg);
void _trusty_read_put_msg_adjust_param(void *);
struct function_description _trusty_read_msg_test = {
    .funcname = "_trusty_read_msg",
    .funcaddr = _trusty_read_msg,
    .argnum = 4,
    .argname = {"handle", "msg_id", "offset", "msg"},
    .argtype = {ARG_TRUSTY_HANDLE, ARG_INT, ARG_INT, ARG_TRUSTY_MSG},
    .argmax[1] = 10,
    .argmax[2] = 4096,
    .argmax[3] = sizeof(struct ipc_msg),
    .do_adjust = _trusty_read_put_msg_adjust_param};
PrepareTest(_trusty_read_msg) { return &_trusty_read_msg_test; }

// long _trusty_put_msg (uint32_t handle, uint32_t msg_id);
struct function_description _trusty_put_msg_test = {
    .funcname = "_trusty_put_msg",
    .funcaddr = _trusty_put_msg,
    .argnum = 2,
    .argname =
        {
            "handle",
            "msg_id",
        },
    .argtype = {ARG_TRUSTY_HANDLE, ARG_INT},
    .do_adjust = _trusty_read_put_msg_adjust_param,
};
PrepareTest(_trusty_put_msg) { return &_trusty_put_msg_test; }

// long _trusty_send_msg (uint32_t handle, struct ipc_msg *msg);
struct function_description _trusty_send_msg_test = {
    .funcname = "_trusty_send_msg",
    .funcaddr = _trusty_send_msg,
    .argnum = 2,
    .argname =
        {
            "handle",
            "msg",
        },
    .argtype = {ARG_TRUSTY_HANDLE, ARG_TRUSTY_MSG},
};
PrepareTest(_trusty_send_msg) { return &_trusty_send_msg_test; }

// long _trusty_mref_create_handle (struct iovec *uiov, uint32_t uiov_cnt,
// uint32_t flags);
uint32_t MREFS_FLAGS[] = {MMAP_FLAG_MEMREF_HANDLE, MMAP_FLAG_PROT_READ,
                          MMAP_FLAG_PROT_WRITE};
struct function_description _trusty_mref_create_handle_test = {
    .funcname = "_trusty_mref_create_handle",
    .funcaddr = _trusty_mref_create_handle,
    .argnum = 3,
    .argname = {"uiov", "uiov_cnt", "flags"},
    .argtype = {ARG_IOV, ARG_LEN, ARG_FLAGS},
    .argselcnt[2] = ARRAY_SIZE(MREFS_FLAGS),
    .argselect[2] = MREFS_FLAGS,
    .argdep[1] = 0 + ARG_DEPEND_MIN};
PrepareTest(_trusty_mref_create_handle) {
  return &_trusty_mref_create_handle_test;
}

// long _trusty_mref_create_child (uint32_t hparent, void *ptr_offset, uint32_t
// size, uint32_t flags);
struct function_description _trusty_mref_create_child_test = {
    .funcname = "_trusty_mref_create_child",
    .funcaddr = _trusty_mref_create_child,
    .argnum = 4,
    .argname = {"hparent", "ptr_offset", "size", "flags"},
    .argtype = {ARG_TRUSTY_HANDLE, ARG_INT, ARG_INT, ARG_FLAGS},
    .argmax[1] = 4096 * 10,
    .argmax[2] = 4096 * 10,
    .argselcnt[3] = ARRAY_SIZE(MREFS_FLAGS),
    .argselect[3] = MREFS_FLAGS,
};
PrepareTest(_trusty_mref_create_child) {
  return &_trusty_mref_create_child_test;
}

// long _trusty_ioctl (uint32_t fd, uint32_t req, void *buf);
uint32_t SYS_PLATFORM_CMDS[] = {SYS_PLATFORM_GET_VENDOR_ID,
                                SYS_PLATFORM_GET_PRODUCT_ID,
                                SYS_PLATFORM_GET_BOARD_NAME,
                                SYS_PLATFORM_GET_RPMB_KEY,
                                SYS_PLATFORM_GET_DEVICE_ID,
                                SYS_PLATFORM_GET_RANDOM_NUMBER,
                                SYS_PLATFORM_GET_RPMB_INFO,
                                SYS_PLATFORM_GET_ALIPAY_ID,
                                SYS_PLATFORM_GET_LOC_DATA_ID,
                                SYS_PLATFORM_GET_HUK,
                                SYS_PLATFORM_GET_BOOT_PARAMETERS,
                                SYS_PLATFORM_GET_NSS_INFO,
                                SYS_PLATFORM_GET_RTC,
                                SYS_PLATFORM_SET_BUF_LEN,
                                SYS_PLATFORM_UNDEFINE};
struct function_description _trusty_ioctl_test = {
    .funcname = "_trusty_ioctl",
    .funcaddr = _trusty_ioctl,
    .argnum = 3,
    .argname = {"fd", "req", "buf"},
    .argtype = {ARG_INT, ARG_SELECT, ARG_ADDRESS},
    .argmax[0] = MAX_SYS_FD_HADLERS,
    .argselcnt[1] = ARRAY_SIZE(SYS_PLATFORM_CMDS),
    .argselect[1] = SYS_PLATFORM_CMDS};
PrepareTest(_trusty_ioctl) { return &_trusty_ioctl_test; };

// long _trusty_nanosleep (uint32_t clock_id, uint32_t flags, void *sleep_time);
struct function_description _trusty_nanosleep_test = {
    .funcname = "_trusty_nanosleep",
    .funcaddr = _trusty_nanosleep,
    .argnum = 3,
    .argname = {"clock_id", "flags", "sleep_time"},
    .argtype = {ARG_INT, ARG_INT, ARG_INT64},
    .block = 1};
PrepareTest(_trusty_nanosleep) { return &_trusty_nanosleep_test; };

// long _trusty_gettime (uint32_t clock_id, uint32_t flags, int64_t *time);
struct function_description _trusty_gettime_test = {
    .funcname = "_trusty_gettime",
    .funcaddr = _trusty_gettime,
    .argnum = 3,
    .argname = {"clock_id", "flags", "time"},
    .argtype = {ARG_INT, ARG_INT, ARG_ADDRESS},
    .argmax[2] = sizeof(uint64_t)};
PrepareTest(_trusty_gettime) { return &_trusty_gettime_test; };

// long _trusty_mmap (void *uaddr, uint32_t size, uint32_t flags, uint32_t
// handle);
uint32_t TRUSTY_MMAP_FLAGS[] = {MMAP_FLAG_IO_HANDLE, MMAP_FLAG_MEMREF_HANDLE};
void _trusty_mmap_ajust_param(void *);
struct function_description _trusty_mmap_test = {
    .funcname = "_trusty_mmap",
    .funcaddr = _trusty_mmap,
    .argnum = 4,
    .argname = {"uaddr", "size", "flags", "handle"},
    .argtype = {ARG_INT64, ARG_INT, ARG_SELECT, ARG_TRUSTY_HANDLE},
    .argmax[0] = 2,
    .argmax[1] = 2 * 4096,
    .argselcnt[2] = ARRAY_SIZE(TRUSTY_MMAP_FLAGS),
    .argselect[2] = TRUSTY_MMAP_FLAGS,
    .do_adjust = _trusty_mmap_ajust_param};
PrepareTest(_trusty_mmap) { return &_trusty_mmap_test; };

// long _trusty_munmap (void *uaddr, uint32_t size);
void _trusty_munmap_ajust_param(void *);
struct function_description _trusty_munmap_test = {
    .funcname = "_trusty_munmap",
    .funcaddr = _trusty_munmap,
    .argnum = 2,
    .argname = {"uddr", "size"},
    .argtype = {ARG_INT64, ARG_INT},
    .argmax[1] = 2 * 4096,
    .do_adjust = _trusty_munmap_ajust_param};
PrepareTest(_trusty_munmap) { return &_trusty_munmap_test; };

// long _trusty_enable_irq(uint32_t process_handle)
static void do_trusty_enable_irq_test(void *arg) {
  (void)arg;
  if (ONE_IN(5)) {
    _trusty_enable_irq(fuz_rnd());
  } else {
    _trusty_enable_irq(zx_process_self());
  }
}
struct function_description _trusty_enable_irq_test = {
    .do_custom_test = do_trusty_enable_irq_test};
PrepareTest(_trusty_enable_irq) { return &_trusty_enable_irq_test; };

// long _trusty_prepare_dma (void *uaddr, uint32_t size, uint32_t flags, struct
// dma_pmem *pmem);
static uint32_t TRUSTY_DMA_FLAGS[] = {DMA_FLAG_TO_DEVICE, DMA_FLAG_FROM_DEVICE,
                                      DMA_FLAG_MULTI_PMEM,
                                      DMA_FLAG_ALLOW_PARTIAL};
struct function_description _trusty_prepare_dma_test = {
    .funcname = "_trusty_prepare_dma",
    .funcaddr = _trusty_prepare_dma,
    .argnum = 4,
    .argname = {"uaddr", "size", "flags", "pmem"},
    .argtype = {ARG_ADDRESS, ARG_LEN, ARG_FLAGS, ARG_ADDRESS},
    .argdep[1] = 0 + ARG_DEPEND_MIN,
    .argselcnt[2] = ARRAY_SIZE(TRUSTY_DMA_FLAGS),
    .argselect[2] = TRUSTY_DMA_FLAGS,
    .argmax[3] = sizeof(struct dma_pmem)};
PrepareTest(_trusty_prepare_dma) { return &_trusty_prepare_dma_test; };

// long _trusty_finish_dma (void *uaddr, uint32_t size, uint32_t flags);
struct function_description _trusty_finish_dma_test = {
    .funcname = "_trusty_finish_dma",
    .funcaddr = _trusty_finish_dma,
    .argnum = 3,
    .argname = {"uaddr", "size", "flags"},
    .argtype =
        {
            ARG_ADDRESS,
            ARG_LEN,
            ARG_FLAGS,
        },
    .argdep[1] = 0 + ARG_DEPEND_MIN,
    .argselcnt[2] = ARRAY_SIZE(TRUSTY_DMA_FLAGS),
    .argselect[2] = TRUSTY_DMA_FLAGS};
PrepareTest(_trusty_finish_dma) { return &_trusty_finish_dma_test; };

// long _trusty_write (uint32_t fd, void *msg, uint32_t size);
// long _trusty_brk (uint32_t brk);
// long _trusty_exit_etc (int32_t status, uint32_t flags);
// long _trusty_read (uint32_t fd, void *msg, uint32_t size);
