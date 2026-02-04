// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <fuz_common.h>

#include <trusty_std.h>
#include <uapi/err.h>

#include <service_utils.h>
#include <storage_utils.h>

#include <nsc_nss.h>
#include <nsc_time.h>

#include "service/nsc/nsc_utils.h"

/**
 * API that doesn't access server, don't need fuzzing.
 * nbl_nsc_get_errno/nbl_nsc_get_channel/nbl_nsc_set_config
 *
 * API
 * nbl_nsc_start/nbl_nsc_stop/nbl_nsc_open/nbl_nsc_read/nbl_nsc_write/
 * nbl_nsc_close/nbl_nsc_fsize/nbl_nsc_lseek/nbl_nsc_remove/nbl_nsc_unlink/
 *
 * nbl_nsc_gettimeofday
 */

static void setup_nsc_session(void *input) {
  (void)input;
  nbl_nsc_config_t config;

  int rc = nbl_nsc_start(&config);
  if (rc != 0) {
    fuz_print_log("Error: %s %d, err: %d\n", __func__, __LINE__,
                  nbl_nsc_get_errno);
    exit(2);
  }
}

static void teardown_nsc_session(void *input) {
  (void)input;
  nbl_nsc_stop();
}

////////////////////////////////////////////////////////////
// Test cases.

////////////////////////////////////////////////////////////
// nbl_nsc_start contains complex source in library,
// but fuzzing needs to test interface to server.
// So I extract some interface from source to small functions.

// int nbl_nsc_get_client_name();
struct function_description nbl_nsc_get_client_name_test = {
    .funcname = "nbl_nsc_get_client_name",
    .funcaddr = nbl_nsc_get_client_name,
    .argnum = 0,
    .argname = {},
};
PrepareTest(nbl_nsc_get_client_name) { return &nbl_nsc_get_client_name_test; }

// int nbl_nsc_send_finish_start_port();
struct function_description nbl_nsc_send_finish_start_port_test = {
    .funcname = "nbl_nsc_send_finish_start_port",
    .funcaddr = nbl_nsc_send_finish_start_port,
    .argnum = 0,
    .argname = {},
};
PrepareTest(nbl_nsc_send_finish_start_port) {
  return &nbl_nsc_send_finish_start_port_test;
}

// nbl_nsc_stop sends information to REE, so it's NOT worth testing.
// But I test it.
// int nbl_nsc_stop();
struct function_description nbl_nsc_stop_test = {
    .funcname = "nbl_nsc_stop",
    .funcaddr = nbl_nsc_stop,
    .argnum = 0,

    .do_setup = setup_nsc_session,
};
PrepareTest(nbl_nsc_stop) { return &nbl_nsc_stop_test; }

// int nbl_nsc_open(const char* nss_path, int flags, ...);
struct function_description nbl_nsc_open_test = {
    .funcname = "nbl_nsc_open",
    .funcaddr = nbl_nsc_open,
    .argnum = 3,
    .argname = {"nss_path", "flags", "mode"},
    .argtype = {ARG_FILE_PATH, ARG_FLAGS, ARG_FLAGS},
    .argselcnt[1] = ARRAY_SIZE(OPEN_FLAGS),
    .argselect[1] = OPEN_FLAGS,
    .argselcnt[2] = ARRAY_SIZE(OPEN_MODES),
    .argselect[2] = OPEN_MODES,

    .do_setup = setup_nsc_session,
    .do_teardown = teardown_nsc_session,
};
PrepareTest(nbl_nsc_open) { return &nbl_nsc_open_test; }

// int nbl_nsc_write(int fd, const void* buf, size_t count);
struct function_description nbl_nsc_write_test = {
    .funcname = "nbl_nsc_write",
    .funcaddr = nbl_nsc_write,
    .argnum = 3,
    .argname = {"fd", "buf", "count"},
    .argtype = {ARG_NSC_FILE_FD, ARG_NORMAL_BUF, ARG_NORMAL_BUF_SIZE},
    .argmax[1] = 4096, // MUST has max value.
    .argdep[2] = 1 + ARG_DEPEND_MIN,

    .do_setup = setup_nsc_session,
    .do_teardown = teardown_nsc_session,
};
PrepareTest(nbl_nsc_write) { return &nbl_nsc_write_test; }

// int nbl_nsc_read(int fd, void* buf, size_t count);
struct function_description nbl_nsc_read_test = {
    .funcname = "nbl_nsc_read",
    .funcaddr = nbl_nsc_read,
    .argnum = 3,
    .argname = {"fd", "buf", "count"},
    .argtype = {ARG_NSC_FILE_FD, ARG_NORMAL_BUF, ARG_NORMAL_BUF_SIZE},
    .argmax[1] = 4096, // MUST has max value.
    .argdep[2] = 1 + ARG_DEPEND_MIN,

    .do_setup = setup_nsc_session,
    .do_teardown = teardown_nsc_session,
};
PrepareTest(nbl_nsc_read) { return &nbl_nsc_read_test; }

// int nbl_nsc_close(int fd);
struct function_description nbl_nsc_close_test = {
    .funcname = "nbl_nsc_close",
    .funcaddr = nbl_nsc_close,
    .argnum = 1,
    .argname = {"fd"},
    .argtype = {ARG_NSC_FILE_FD},

    .do_setup = setup_nsc_session,
    .do_teardown = teardown_nsc_session,
};
PrepareTest(nbl_nsc_close) { return &nbl_nsc_close_test; }

// int nbl_nsc_fsize(int fd);
struct function_description nbl_nsc_fsize_test = {
    .funcname = "nbl_nsc_fsize",
    .funcaddr = nbl_nsc_fsize,
    .argnum = 1,
    .argname = {"fd"},
    .argtype = {ARG_NSC_FILE_FD},

    .do_setup = setup_nsc_session,
    .do_teardown = teardown_nsc_session,
};
PrepareTest(nbl_nsc_fsize) { return &nbl_nsc_fsize_test; }

// off_t nbl_nsc_lseek(int fd, off_t offset, int whence);
struct function_description nbl_nsc_lseek_test = {
    .funcname = "nbl_nsc_lseek",
    .funcaddr = nbl_nsc_lseek,
    .argnum = 3,
    .argname = {"fd", "offset", "whence"},
    .argtype = {ARG_NSC_FILE_FD, ARG_INT64, ARG_FLAGS},

    .argselcnt[2] = ARRAY_SIZE(SEEK_FLAGS),
    .argselect[2] = SEEK_FLAGS,

    .do_setup = setup_nsc_session,
    .do_teardown = teardown_nsc_session,
};
PrepareTest(nbl_nsc_lseek) { return &nbl_nsc_lseek_test; }

// int nbl_nsc_unlink(const char* path);
// deleting file is done in REE, so don't care valid file name.
struct function_description nbl_nsc_unlink_test = {
    .funcname = "nbl_nsc_unlink",
    .funcaddr = nbl_nsc_unlink,
    .argnum = 1,
    .argname = {"path"},
    .argtype = {ARG_FILE_PATH},

    .do_setup = setup_nsc_session,
    .do_teardown = teardown_nsc_session,
};
PrepareTest(nbl_nsc_unlink) { return &nbl_nsc_unlink_test; }

// int nbl_nsc_remove(const char* path);
// deleting file is done in REE, so don't care valid file name.
struct function_description nbl_nsc_remove_test = {
    .funcname = "nbl_nsc_remove",
    .funcaddr = nbl_nsc_remove,
    .argnum = 1,
    .argname = {"path"},
    .argtype = {ARG_FILE_PATH},

    .do_setup = setup_nsc_session,
    .do_teardown = teardown_nsc_session,
};
PrepareTest(nbl_nsc_remove) { return &nbl_nsc_remove_test; }

////////////////////////////////////////////////////////////
// nsc_time.h

// int nbl_nsc_gettimeofday(struct timeval *tv, struct timezone *tz);
// tz is not used in function, keep it just for old interface.
struct function_description nbl_nsc_gettimeofday_test = {
    .funcname = "nbl_nsc_gettimeofday",
    .funcaddr = nbl_nsc_gettimeofday,
    .argnum = 2,
    .argname = {"tv", "tz"},
    .argtype = {ARG_NORMAL_BUF, ARG_NORMAL_BUF},

    .argmax[0] = sizeof(struct timeval),
    .argmin[0] = sizeof(struct timeval),
    .argmax[1] = sizeof(struct timezone),
    .argmin[1] = sizeof(struct timezone),

    .do_setup = setup_nsc_session,
    .do_teardown = teardown_nsc_session,
};
PrepareTest(nbl_nsc_gettimeofday) { return &nbl_nsc_gettimeofday_test; }
