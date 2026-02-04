// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <fuz_common.h>

#include "zx_syscall_test.h"
#include <zircon/syscalls.h>
#include <zircon/syscalls/policy.h>
#include <zircon/syscalls/port.h>

struct function_description zx_ticks_get_test = {
    .funcname = "zx_ticks_get", .funcaddr = zx_ticks_get, .argnum = 0};

PrepareTest(zx_ticks_get) { return &zx_ticks_get_test; }

struct function_description zx_ticks_per_second_test = {
    .funcname = "zx_ticks_per_second",
    .funcaddr = zx_ticks_per_second,
    .argnum = 0};

PrepareTest(zx_ticks_per_second) { return &zx_ticks_per_second_test; }

struct function_description zx_system_get_dcache_line_size_test = {
    .funcname = "zx_system_get_dcache_line_size",
    .funcaddr = zx_system_get_dcache_line_size,
    .argnum = 0};

PrepareTest(zx_system_get_dcache_line_size) {
  return &zx_system_get_dcache_line_size_test;
}

struct function_description zx_system_get_num_cpus_test = {
    .funcname = "zx_system_get_num_cpus",
    .funcaddr = zx_system_get_num_cpus,
    .argnum = 0};

PrepareTest(zx_system_get_num_cpus) { return &zx_system_get_num_cpus_test; }

struct function_description zx_system_get_version_test = {
    .funcname = "zx_system_get_version",
    .funcaddr = zx_system_get_version,
    .argnum = 2,
    .argname = {"version", // Parameter NULL causes user space to crash
                "version_len"},
    .argtype = {ARG_ADDRESS, ARG_LEN},
    .argdep[1] = 0 + ARG_DEPEND_MIN,
};

PrepareTest(zx_system_get_version) { return &zx_system_get_version_test; }

struct function_description zx_system_get_physmem_test = {
    .funcname = "zx_system_get_physmem",
    .funcaddr = zx_system_get_physmem,
    .argnum = 0};

PrepareTest(zx_system_get_physmem_cpus) { return &zx_system_get_physmem_test; }

struct function_description zx_system_get_features_test = {
    .funcname = "zx_system_get_features",
    .funcaddr = zx_system_get_features,
    .argnum = 2,
    .argname = {"kind", "out"},
    .argtype = {
        ARG_INT,
        ARG_NORMAL_BUF // ARG_ADDRESS invalid pointer will cause user space to
                       // crash.
    }};

PrepareTest(zx_system_get_features) { return &zx_system_get_features_test; }

static uint32_t ZX_CACHE_FLUSH_FLAGS[] = {
    ZX_CACHE_FLUSH_INSN, ZX_CACHE_FLUSH_DATA, ZX_CACHE_FLUSH_INVALIDATE};

struct function_description zx_cache_flush_test = {
    .funcname = "zx_cache_flush",
    .funcaddr = zx_cache_flush,
    .argnum = 3,
    .argname = {"addr", "len", "options"},
    .argtype = {ARG_ADDRESS, ARG_LEN, ARG_FLAGS},
    .argdep[1] = 0 + ARG_DEPEND_MIN,
    .argselcnt[2] = ARRAY_SIZE(ZX_CACHE_FLUSH_FLAGS),
    .argselect[2] = ZX_CACHE_FLUSH_FLAGS};

PrepareTest(zx_cache_flush) { return &zx_cache_flush_test; }

struct function_description zx_cprng_add_entropy_test = {
    .funcname = "zx_cprng_add_entropy",
    .funcaddr = zx_cprng_add_entropy,
    .argnum = 2,
    .argname = {"buffer", "len"},
    .argtype = {ARG_ADDRESS, ARG_LEN},
    .argdep[1] = 0 + ARG_DEPEND_MIN};

PrepareTest(zx_cprng_add_entropy) { return &zx_cprng_add_entropy_test; }

struct function_description zx_debug_write_test = {
    .funcname = "zx_debug_write",
    .funcaddr = zx_debug_write,
    .argnum = 2,
    .argname = {"buffer", "length"},
    .argtype = {ARG_ADDRESS, ARG_LEN},
    .argdep[1] = 0 + ARG_DEPEND_MIN};

PrepareTest(zx_debug_write) { return &zx_debug_write_test; }

struct function_description zx_bootloader_fb_get_info_test = {
    .funcname = "zx_bootloader_fb_get_info",
    .funcaddr = zx_bootloader_fb_get_info,
    .argnum = 4,
    .argname = {"format", "width", "height", "stride"},
    .argtype = {ARG_ADDRESS, ARG_ADDRESS, ARG_ADDRESS, ARG_ADDRESS}};

PrepareTest(zx_bootloader_fb_get_info) {
  return &zx_bootloader_fb_get_info_test;
}

struct function_description zx_nanosleep_test = {.funcname = "zx_nanosleep",
                                                 .funcaddr = zx_nanosleep,
                                                 .argnum = 1,
                                                 .argname = {"deadline"},
                                                 .argtype = {ARG_INT64},
                                                 .block = 1};

PrepareTest(zx_nanosleep) { return &zx_nanosleep_test; }

struct function_description zx_deadline_after_test = {
    .funcname = "zx_deadline_after",
    .funcaddr = zx_deadline_after,
    .argnum = 1,
    .argname = {"nanoseconds"},
    .argtype = {ARG_INT64}};

PrepareTest(zx_deadline_after) { return &zx_deadline_after_test; }

static uint32_t ZX_RIGHT_FLAGS[] = {ZX_RIGHT_NONE,
                                    ZX_RIGHT_DUPLICATE,
                                    ZX_RIGHT_TRANSFER,
                                    ZX_RIGHT_READ,
                                    ZX_RIGHT_WRITE,
                                    ZX_RIGHT_EXECUTE,
                                    ZX_RIGHT_MAP,
                                    ZX_RIGHT_GET_PROPERTY,
                                    ZX_RIGHT_SET_PROPERTY,
                                    ZX_RIGHT_ENUMERATE,
                                    ZX_RIGHT_DESTROY,
                                    ZX_RIGHT_SET_POLICY,
                                    ZX_RIGHT_GET_POLICY,
                                    ZX_RIGHT_SIGNAL,
                                    ZX_RIGHT_SIGNAL_PEER,
                                    ZX_RIGHT_WAIT,
                                    ZX_RIGHT_INSPECT,
                                    ZX_RIGHT_MANAGE_JOB,
                                    ZX_RIGHT_MANAGE_PROCESS,
                                    ZX_RIGHT_MANAGE_THREAD,
                                    ZX_RIGHT_APPLY_PROFILE,
                                    ZX_RIGHT_SAME_RIGHTS};

struct function_description zx_handle_duplicate_test = {
    .funcname = "zx_handle_duplicate",
    .funcaddr = zx_handle_duplicate,
    .argnum = 2,
    .argname = {"handle", "rights", "out"},
    .argtype = {ARG_ZX_HANDLE, ARG_FLAGS, ARG_ADDRESS},
    .argselcnt[1] = ARRAY_SIZE(ZX_RIGHT_FLAGS),
    .argselect[1] = ZX_RIGHT_FLAGS};

PrepareTest(zx_handle_duplicate) { return &zx_handle_duplicate_test; }

struct function_description zx_handle_replace_test = {
    .funcname = "zx_handle_replace",
    .funcaddr = zx_handle_replace,
    .argnum = 2,
    .argname = {"handle", "rights", "out"},
    .argtype = {ARG_ZX_HANDLE, ARG_FLAGS, ARG_ADDRESS},
    .argselcnt[1] = ARRAY_SIZE(ZX_RIGHT_FLAGS),
    .argselect[1] = ZX_RIGHT_FLAGS};

PrepareTest(zx_handle_replace) { return &zx_handle_replace_test; }

static uint32_t ZX_OBJECT_SIGNAL_FLAGS[] = {
    __ZX_OBJECT_READABLE,  __ZX_OBJECT_WRITABLE,  __ZX_OBJECT_PEER_CLOSED,
    __ZX_OBJECT_SIGNALED,  __ZX_OBJECT_SIGNAL_4,  __ZX_OBJECT_SIGNAL_5,
    __ZX_OBJECT_SIGNAL_6,  __ZX_OBJECT_SIGNAL_7,  __ZX_OBJECT_SIGNAL_8,
    __ZX_OBJECT_SIGNAL_9,  __ZX_OBJECT_SIGNAL_10, __ZX_OBJECT_SIGNAL_11,
    __ZX_OBJECT_SIGNAL_12, __ZX_OBJECT_SIGNAL_13, __ZX_OBJECT_SIGNAL_14,
    __ZX_OBJECT_SIGNAL_15, __ZX_OBJECT_SIGNAL_16, __ZX_OBJECT_SIGNAL_17,
    __ZX_OBJECT_SIGNAL_18, __ZX_OBJECT_SIGNAL_19, __ZX_OBJECT_SIGNAL_20,
    __ZX_OBJECT_SIGNAL_21, __ZX_OBJECT_SIGNAL_22, __ZX_OBJECT_HANDLE_CLOSED};

struct function_description zx_object_wait_one_test = {
    .funcname = "zx_object_wait_one",
    .funcaddr = zx_object_wait_one,
    .argnum = 4,
    .argname = {"handle", "waitfor", "deadline", "observed"},
    .argtype = {ARG_ZX_HANDLE, ARG_FLAGS, ARG_INT64, ARG_ADDRESS},
    .argselcnt[1] = ARRAY_SIZE(ZX_OBJECT_SIGNAL_FLAGS),
    .argselect[1] = ZX_OBJECT_SIGNAL_FLAGS};

PrepareTest(zx_object_wait_one) { return &zx_object_wait_one_test; }

struct function_description zx_object_wait_many_test = {
    .funcname = "zx_object_wait_many_fun",
    .funcaddr = zx_object_wait_many_fun,
    .argnum = 3,
    .argname = {"items", "count", "deadline"},
    .argtype = {ARG_ADDRESS, ARG_INT, ARG_INT64},
    .block = 1};

PrepareTest(zx_object_wait_many_fun) { return &zx_object_wait_many_test; }

static uint32_t ZX_WAIT_ASYNC_TEST_FLAGS1[] = {ZX_WAIT_ASYNC_ONCE,
                                               ZX_WAIT_ASYNC_REPEATING};

struct function_description zx_object_wait_async_test = {
    .funcname = "zx_object_wait_async",
    .funcaddr = zx_object_wait_async,
    .argnum = 5,
    .argname = {"handle", "port_handle", "key", "signals", "options"},
    .argtype = {ARG_ZX_HANDLE, ARG_ZX_HANDLE, ARG_INT64, ARG_FLAGS, ARG_FLAGS},
    .argselcnt[3] = ARRAY_SIZE(ZX_OBJECT_SIGNAL_FLAGS),
    .argselect[3] = ZX_OBJECT_SIGNAL_FLAGS,
    .argselcnt[4] = ARRAY_SIZE(ZX_WAIT_ASYNC_TEST_FLAGS1),
    .argselect[4] = ZX_WAIT_ASYNC_TEST_FLAGS1};

PrepareTest(zx_object_wait_async) { return &zx_object_wait_async_test; }

struct function_description zx_object_signal_test = {
    .funcname = "zx_object_signal",
    .funcaddr = zx_object_signal,
    .argnum = 3,
    .argname = {"handle", "clear_mask", "set_mask"},
    .argtype = {ARG_ZX_HANDLE, ARG_FLAGS, ARG_FLAGS},
    .argselcnt[1] = ARRAY_SIZE(ZX_OBJECT_SIGNAL_FLAGS),
    .argselect[1] = ZX_OBJECT_SIGNAL_FLAGS,
    .argselcnt[2] = ARRAY_SIZE(ZX_OBJECT_SIGNAL_FLAGS),
    .argselect[2] = ZX_OBJECT_SIGNAL_FLAGS};

PrepareTest(zx_object_signal) { return &zx_object_signal_test; }

struct function_description zx_object_signal_peer_test = {
    .funcname = "zx_object_signal_peer",
    .funcaddr = zx_object_signal_peer,
    .argnum = 3,
    .argname = {"handle", "clear_mask", "set_mask"},
    .argtype = {ARG_ZX_HANDLE, ARG_FLAGS, ARG_FLAGS},
    .argselcnt[1] = ARRAY_SIZE(ZX_OBJECT_SIGNAL_FLAGS),
    .argselect[1] = ZX_OBJECT_SIGNAL_FLAGS,
    .argselcnt[2] = ARRAY_SIZE(ZX_OBJECT_SIGNAL_FLAGS),
    .argselect[2] = ZX_OBJECT_SIGNAL_FLAGS};

PrepareTest(zx_object_signal_peer) { return &zx_object_signal_peer_test; }

static uint32_t ZX_PROPERTY_TEST_FLAGS[] = {
    ZX_INFO_CPU_STATS_FLAG_ONLINE, ZX_PROP_NAME,
    ZX_PROP_PROCESS_DEBUG_ADDR,    ZX_PROP_PROCESS_VDSO_BASE_ADDRESS,
    ZX_PROP_JOB_IMPORTANCE,        ZX_PROP_SOCKET_RX_BUF_MAX,
    ZX_PROP_SOCKET_RX_BUF_SIZE,    ZX_PROP_SOCKET_TX_BUF_MAX,
    ZX_PROP_SOCKET_TX_BUF_SIZE};

struct function_description zx_object_get_property_test = {
    .funcname = "zx_object_get_property",
    .funcaddr = zx_object_get_property,
    .argnum = 4,
    .argname = {"handle", "property", "value", "size"},
    .argtype = {ARG_ZX_HANDLE, ARG_FLAGS, ARG_ADDRESS, ARG_FLAGS},
    .argdep[3] = 2 + ARG_DEPEND_MIN,
    .argselcnt[1] = ARRAY_SIZE(ZX_PROPERTY_TEST_FLAGS),
    .argselect[1] = ZX_PROPERTY_TEST_FLAGS};

PrepareTest(zx_object_get_property) { return &zx_object_get_property_test; }

struct function_description zx_object_set_property_test = {
    .funcname = "zx_object_set_property",
    .funcaddr = zx_object_set_property,
    .argnum = 4,
    .argname = {"handle", "property", "value", "size"},
    .argtype = {ARG_ZX_HANDLE, ARG_FLAGS, ARG_ADDRESS, ARG_FLAGS},
    .argdep[3] = 2 + ARG_DEPEND_MIN,
    .argselcnt[1] = ARRAY_SIZE(ZX_PROPERTY_TEST_FLAGS),
    .argselect[1] = ZX_PROPERTY_TEST_FLAGS};

PrepareTest(zx_object_set_property) { return &zx_object_set_property_test; }

struct function_description zx_object_set_cookie_test = {
    .funcname = "zx_object_set_cookie",
    .funcaddr = zx_object_set_cookie,
    .argnum = 3,
    .argname = {"handle", "scope", "cookie"},
    .argtype = {ARG_ZX_HANDLE, ARG_ZX_HANDLE, ARG_INT64}};

PrepareTest(zx_object_set_cookie) { return &zx_object_set_cookie_test; }

struct function_description zx_object_get_cookie_test = {
    .funcname = "zx_object_get_cookie",
    .funcaddr = zx_object_get_cookie,
    .argnum = 3,
    .argname = {"handle", "scope", "cookie"},
    .argtype = {ARG_ZX_HANDLE, ARG_ZX_HANDLE, ARG_ADDRESS}};

PrepareTest(zx_object_get_cookie) { return &zx_object_get_cookie_test; }

static uint32_t ZX_INFO_TEST_FLAGS[] = {ZX_INFO_NONE,
                                        ZX_INFO_HANDLE_VALID,
                                        ZX_INFO_HANDLE_BASIC,
                                        ZX_INFO_PROCESS,
                                        ZX_INFO_PROCESS_THREADS,
                                        ZX_INFO_VMAR,
                                        ZX_INFO_JOB_CHILDREN,
                                        ZX_INFO_JOB_PROCESSES,
                                        ZX_INFO_THREAD,
                                        ZX_INFO_THREAD_EXCEPTION_REPORT,
                                        ZX_INFO_TASK_STATS,
                                        ZX_INFO_PROCESS_MAPS,
                                        ZX_INFO_PROCESS_VMOS,
                                        ZX_INFO_THREAD_STATS,
                                        ZX_INFO_CPU_STATS,
                                        ZX_INFO_KMEM_STATS,
                                        ZX_INFO_RESOURCE,
                                        ZX_INFO_HANDLE_COUNT,
                                        ZX_INFO_BTI,
                                        ZX_INFO_PROCESS_HANDLE_STATS,
                                        ZX_INFO_LAST};

struct function_description zx_object_get_info_test = {
    .funcname = "zx_object_get_info",
    .funcaddr = zx_object_get_info,
    .argnum = 6,
    .argname = {"handle", "topic", "buffer", "buffer_size", "actual_count",
                "avail_count"},
    .argtype = {ARG_ZX_HANDLE, ARG_FLAGS, ARG_ADDRESS, ARG_LEN, ARG_ADDRESS,
                ARG_ADDRESS},
    .argselcnt[1] = ARRAY_SIZE(ZX_INFO_TEST_FLAGS),
    .argselect[1] = ZX_INFO_TEST_FLAGS,
    .argdep[3] = 2 + ARG_DEPEND_MIN};

PrepareTest(zx_object_get_info) { return &zx_object_get_info_test; }

struct function_description zx_object_get_child_test = {
    .funcname = "zx_object_get_child",
    .funcaddr = zx_object_get_child,
    .argnum = 4,
    .argname = {"handle", "koid", "rights", "out"},
    .argtype = {ARG_ZX_HANDLE, ARG_INT64, ARG_FLAGS, ARG_ADDRESS},
    .argselcnt[2] = ARRAY_SIZE(ZX_RIGHT_FLAGS),
    .argselect[2] = ZX_RIGHT_FLAGS,
};

PrepareTest(zx_object_get_child) { return &zx_object_get_child_test; }

struct function_description zx_object_set_profile_test = {
    .funcname = "zx_object_set_profile",
    .funcaddr = zx_object_set_profile,
    .argnum = 3,
    .argname = {"handle", "profile", "options"},
    .argtype = {ARG_ZX_HANDLE, ARG_ZX_HANDLE, ARG_INT},
    .argselcnt[2] = ARRAY_SIZE(ZX_RIGHT_FLAGS),
    .argselect[2] = ZX_RIGHT_FLAGS,
};

PrepareTest(zx_object_set_profile) { return &zx_object_set_profile_test; }

struct function_description zx_channel_create_test = {
    .funcname = "zx_channel_create",
    .funcaddr = zx_channel_create,
    .argnum = 3,
    .argname = {"options", "out0", "out1"},
    .argtype = {ARG_INT, ARG_ADDRESS, ARG_ADDRESS}};

PrepareTest(zx_channel_create) { return &zx_channel_create_test; }

struct function_description zx_channel_read_test = {
    .funcname = "zx_channel_read",
    .funcaddr = zx_channel_read,
    .argnum = 8,
    .argname = {"handle", "options", "bytes", "handles", "num_bytes",
                "num_handles", "actual_bytes", "actual_handles"},
    .argtype = {ARG_ZX_CHANNEL_HANDLE, ARG_INT, ARG_ADDRESS, ARG_ZX_HANDLE,
                ARG_LEN, ARG_INT, ARG_ADDRESS, ARG_ADDRESS},
    .argdep[4] = 2 + ARG_DEPEND_MIN};

PrepareTest(zx_channel_read) { return &zx_channel_read_test; }

struct function_description zx_channel_write_test = {
    .funcname = "zx_channel_write",
    .funcaddr = zx_channel_write,
    .argnum = 6,
    .argname = {"handle", "options", "bytes", "num_bytes",
                "handles"
                "num_handles"},
    .argtype = {ARG_ZX_CHANNEL_HANDLE, ARG_INT, ARG_ADDRESS, ARG_LEN,
                ARG_ADDRESS, ARG_INT},
    .argdep[3] = 2 + ARG_DEPEND_MIN};

PrepareTest(zx_channel_write) { return &zx_channel_write_test; }

struct function_description zx_channel_call_test = {
    .funcname = "zx_channel_call",
    .funcaddr = zx_channel_call,
    .argnum = 7,
    .argname = {"handle", "options", "deadline", "args", "actual_bytes",
                "actual_handles", "read_status"},
    .argtype = {ARG_ZX_CHANNEL_HANDLE, ARG_INT, ARG_INT64, ARG_ADDRESS,
                ARG_ADDRESS, ARG_ADDRESS, ARG_ADDRESS}};

PrepareTest(zx_channel_call) { return &zx_channel_call_test; }

static uint32_t ZX_SOCKET_TEST_FLAGS[] = {
    ZX_SOCKET_STREAM, ZX_SOCKET_DATAGRAM, ZX_SOCKET_HAS_CONTROL,
    ZX_SOCKET_HAS_ACCEPT, ZX_SOCKET_CREATE_MASK};

struct function_description zx_socket_create_test = {
    .funcname = "zx_socket_create",
    .funcaddr = zx_socket_create,
    .argnum = 3,
    .argname = {"options", "out0", "out1"},
    .argtype = {ARG_FLAGS, ARG_ADDRESS, ARG_ADDRESS},
    .argselcnt[0] = ARRAY_SIZE(ZX_SOCKET_TEST_FLAGS),
    .argselect[0] = ZX_SOCKET_TEST_FLAGS};

PrepareTest(zx_socket_create) { return &zx_socket_create_test; }

static uint32_t ZX_SOCKET_SHUTDOWN_TEST_FLAGS[] = {
    ZX_SOCKET_SHUTDOWN_WRITE, ZX_SOCKET_SHUTDOWN_READ, ZX_SOCKET_SHUTDOWN_MASK};

struct function_description zx_socket_write_test = {
    .funcname = "zx_socket_write",
    .funcaddr = zx_socket_write,
    .argnum = 5,
    .argname = {"handle", "options", "buffer", "size", "actual"},
    .argtype = {ARG_ZX_SOCKET_HANDLE, ARG_FLAGS, ARG_ADDRESS, ARG_LEN,
                ARG_ADDRESS},
    .argselcnt[1] = ARRAY_SIZE(ZX_SOCKET_SHUTDOWN_TEST_FLAGS),
    .argselect[1] = ZX_SOCKET_SHUTDOWN_TEST_FLAGS,
    .argdep[3] = 2 + ARG_DEPEND_MIN};

PrepareTest(zx_socket_write) { return &zx_socket_write_test; }

struct function_description zx_socket_read_test = {
    .funcname = "zx_socket_read",
    .funcaddr = zx_socket_read,
    .argnum = 5,
    .argname = {"handle", "options", "buffer", "size", "actual"},
    .argtype = {ARG_ZX_SOCKET_HANDLE, ARG_INT, ARG_ADDRESS, ARG_LEN,
                ARG_ADDRESS},
    .argdep[3] = 2 + ARG_DEPEND_MIN};

PrepareTest(zx_socket_read) { return &zx_socket_read_test; }

struct function_description zx_socket_share_test = {
    .funcname = "zx_socket_share",
    .funcaddr = zx_socket_share,
    .argnum = 2,
    .argname = {"handle", "socket_to_share"},
    .argtype = {ARG_ZX_SOCKET_HANDLE, ARG_ZX_SOCKET_HANDLE}};

PrepareTest(zx_socket_share) { return &zx_socket_share_test; }

struct function_description zx_socket_accept_test = {
    .funcname = "zx_socket_accept",
    .funcaddr = zx_socket_accept,
    .argnum = 2,
    .argname = {"handle", "out_socket"},
    .argtype = {ARG_ZX_SOCKET_HANDLE, ARG_ADDRESS}};

PrepareTest(zx_socket_accept) { return &zx_socket_accept_test; }

struct function_description zx_thread_create_test = {
    .funcname = "zx_thread_create",
    .funcaddr = zx_thread_create,
    .argnum = 5,
    .argname = {"process", "name", "name_len", "options", "out"},
    .argtype = {ARG_ZX_PROCESS_HANDLE, ARG_ADDRESS, ARG_LEN, ARG_INT,
                ARG_ADDRESS},
    .argdep[2] = 1 + ARG_DEPEND_MIN};

PrepareTest(zx_thread_create) { return &zx_thread_create_test; }

struct function_description zx_thread_start_test = {
    .funcname = "zx_thread_start",
    .funcaddr = zx_thread_start,
    .argnum = 5,
    .argname = {"handle", "thread_entry", "stack", "arg1", "arg2"},
    .argtype = {ARG_ZX_THREAD_HANDLE,
                ARG_ADDRESS, // Parameter 0 causes user space to crash
                ARG_ADDRESS, // Parameter 0 causes user space to crash
                ARG_ADDRESS, ARG_ADDRESS},
    .block = 1};

PrepareTest(zx_thread_start) { return &zx_thread_start_test; }

struct function_description zx_thread_read_state_test = {
    .funcname = "zx_thread_read_state_fun",
    .funcaddr = zx_thread_read_state_fun,
    .argnum = 4,
    .argname = {"handle", "kind", "buffer", "len"},
    .argtype = {ARG_ZX_THREAD_HANDLE, ARG_INT, ARG_ADDRESS, ARG_LEN},
    .argdep[3] = 2 + ARG_DEPEND_MIN};

PrepareTest(zx_thread_read_state_fun) { return &zx_thread_read_state_test; }

struct function_description zx_thread_write_state_test = {
    .funcname = "zx_thread_write_state_fun",
    .funcaddr = zx_thread_write_state_fun,
    .argnum = 4,
    .argname = {"handle", "kind", "buffer", "buffer_len"},
    .argtype = {ARG_ZX_THREAD_HANDLE, ARG_INT, ARG_ADDRESS, ARG_LEN},
    .argdep[3] = 2 + ARG_DEPEND_MIN};

PrepareTest(zx_thread_write_state_fun) { return &zx_thread_write_state_test; }

struct function_description zx_thread_set_priority_test = {
    .funcname = "zx_thread_set_priority",
    .funcaddr = zx_thread_set_priority,
    .argnum = 1,
    .argname =
        {
            "prio",
        },
    .argtype = {ARG_INT},
    .block = 1};

PrepareTest(zx_thread_set_priority) { return &zx_thread_set_priority_test; }

struct function_description zx_process_create_test = {
    .funcname = "zx_process_create",
    .funcaddr = zx_process_create,
    .argnum = 6,
    .argname = {"job", "name", "name_len", "options", "proc_handle",
                "vmar_handle"},
    .argtype = {ARG_ZX_JOB_HANDLE, ARG_ADDRESS, ARG_LEN, ARG_INT, ARG_ADDRESS,
                ARG_ADDRESS},
    .argdep[2] = 1 + ARG_DEPEND_MIN};

PrepareTest(zx_process_create) { return &zx_process_create_test; }

struct function_description zx_process_start_test = {
    .funcname = "zx_process_start",
    .funcaddr = zx_process_start,
    .argnum = 6,
    .argname = {"process_handle", "thread_handle", "entry", "stack",
                "arg_handle", "arg2"},
    .argtype = {ARG_ZX_PROCESS_HANDLE, ARG_ZX_THREAD_HANDLE, ARG_ADDRESS,
                ARG_ADDRESS, ARG_ZX_HANDLE, ARG_ADDRESS}};

PrepareTest(zx_process_start) { return &zx_process_start_test; }

struct function_description zx_process_read_memory_test = {
    .funcname = "zx_process_read_memory",
    .funcaddr = zx_process_read_memory,
    .argnum = 5,
    .argname = {"proc", "vaddr", "buffer", "len", "actual"},
    .argtype = {ARG_ZX_PROCESS_HANDLE, ARG_ADDRESS, ARG_ADDRESS, ARG_LEN,
                ARG_ADDRESS},
    .argdep[3] = 2 + ARG_DEPEND_MIN};

PrepareTest(zx_process_read_memory) { return &zx_process_read_memory_test; }

struct function_description zx_process_write_memory_test = {
    .funcname = "zx_process_write_memory",
    .funcaddr = zx_process_write_memory,
    .argnum = 5,
    .argname = {"proc", "vaddr", "buffer", "len", "actual"},
    .argtype = {ARG_ZX_PROCESS_HANDLE, ARG_ADDRESS, ARG_ADDRESS, ARG_LEN,
                ARG_ADDRESS},
    .argdep[3] = 2 + ARG_DEPEND_MIN};

PrepareTest(zx_process_write_memory) { return &zx_process_write_memory_test; }

static uint32_t ZX_JOB_BY_PARENT_POLICY_FLAGS[] = {ZX_JOB_POL_RELATIVE,
                                                   ZX_JOB_POL_ABSOLUTE};

static uint32_t ZX_JOB_BY_BASIC_POLICY_FLAGS[] = {ZX_JOB_POL_BASIC};

struct function_description zx_job_create_test = {
    .funcname = "zx_job_create",
    .funcaddr = zx_job_create,
    .argnum = 3,
    .argname = {"parent_job", "options", "out"},
    .argtype = {ARG_ZX_JOB_HANDLE, ARG_INT, ARG_ADDRESS}};

PrepareTest(zx_job_create) { return &zx_job_create_test; }

struct function_description zx_job_set_policy_test = {
    .funcname = "zx_job_set_policy",
    .funcaddr = zx_job_set_policy,
    .argnum = 5,
    .argname = {"job", "options", "topic", "policy", "count"},
    .argtype = {ARG_ZX_JOB_HANDLE, ARG_FLAGS, ARG_FLAGS, ARG_ADDRESS, ARG_INT},
    .argselcnt[1] = ARRAY_SIZE(ZX_JOB_BY_PARENT_POLICY_FLAGS),
    .argselect[1] = ZX_JOB_BY_PARENT_POLICY_FLAGS,
    .argselcnt[2] = ARRAY_SIZE(ZX_JOB_BY_BASIC_POLICY_FLAGS),
    .argselect[2] = ZX_JOB_BY_BASIC_POLICY_FLAGS,
};

PrepareTest(zx_job_set_policy) { return &zx_job_set_policy_test; }

struct function_description zx_task_bind_exception_port_test = {
    .funcname = "zx_task_bind_exception_port",
    .funcaddr = zx_task_bind_exception_port,
    .argnum = 4,
    .argname = {"object", "eport", "key", "options"},
    .argtype = {ARG_ZX_THREAD_HANDLE, ARG_ZX_HANDLE, ARG_INT, ARG_INT64},
    .argselcnt[1] = ARRAY_SIZE(ZX_JOB_BY_PARENT_POLICY_FLAGS),
    .argselect[1] = ZX_JOB_BY_PARENT_POLICY_FLAGS,
    .argselcnt[2] = ARRAY_SIZE(ZX_JOB_BY_BASIC_POLICY_FLAGS),
    .argselect[2] = ZX_JOB_BY_BASIC_POLICY_FLAGS,
};

PrepareTest(zx_task_bind_exception_port) {
  return &zx_task_bind_exception_port_test;
}

struct function_description zx_task_suspend_test = {
    .funcname = "zx_task_suspend",
    .funcaddr = zx_task_suspend,
    .argnum = 1,
    .argname = {"task_handle"},
    .argtype = {ARG_ZX_THREAD_HANDLE}};

PrepareTest(zx_task_suspend) { return &zx_task_suspend_test; }

struct function_description zx_task_resume_test = {
    .funcname = "zx_task_resume",
    .funcaddr = zx_task_resume,
    .argnum = 2,
    .argname = {"task_handle", "options"},
    .argtype = {ARG_ZX_THREAD_HANDLE, ARG_INT}};

PrepareTest(zx_task_resume) { return &zx_task_resume_test; }

struct function_description zx_task_kill_test = {
    .funcname = "zx_task_kill",
    .funcaddr = zx_task_kill,
    .argnum = 1,
    .argname = {"task_handle"},
    .argtype = {ARG_ZX_PROCESS_HANDLE},
    .block = 1};

PrepareTest(zx_task_kill) { return &zx_task_kill_test; }

struct function_description zx_event_create_test = {
    .funcname = "zx_event_create",
    .funcaddr = zx_event_create,
    .argnum = 1,
    .argname = {"options", "out"},
    .argtype = {ARG_INT, ARG_ADDRESS}};

PrepareTest(zx_event_create) { return &zx_event_create_test; }

struct function_description zx_eventpair_create_test = {
    .funcname = "zx_eventpair_create",
    .funcaddr = zx_eventpair_create,
    .argnum = 3,
    .argname = {"options", "out", "out1"},
    .argtype = {ARG_INT, ARG_ADDRESS, ARG_ADDRESS}};

PrepareTest(zx_eventpair_create) { return &zx_eventpair_create_test; }

struct function_description zx_futex_wait_test = {
    .funcname = "zx_futex_wait",
    .funcaddr = zx_futex_wait,
    .argnum = 3,
    .argname = {"value_ptr", "current_value", "deadline"},
    .argtype = {ARG_ADDRESS, ARG_INT, ARG_INT64},
    .block = 1};

PrepareTest(zx_futex_wait) { return &zx_futex_wait_test; }

struct function_description zx_futex_wake_test = {
    .funcname = "zx_futex_wake",
    .funcaddr = zx_futex_wake,
    .argnum = 2,
    .argname = {"value_ptr", "count"},
    .argtype = {ARG_ADDRESS, ARG_INT},
    .block = 1};

PrepareTest(zx_futex_wake) { return &zx_futex_wake_test; }

struct function_description zx_futex_requeue_test = {
    .funcname = "zx_futex_requeue",
    .funcaddr = zx_futex_requeue,
    .argnum = 5,
    .argname = {"wake_ptr", "wake_count", "current_value", "requeue_ptr",
                "requeue_count"},
    .argtype = {ARG_ADDRESS, ARG_INT, ARG_INT, ARG_ADDRESS, ARG_INT},
    .block = 1};

PrepareTest(zx_futex_requeue) { return &zx_futex_requeue_test; }

struct function_description zx_port_create_test = {
    .funcname = "zx_port_create",
    .funcaddr = zx_port_create,
    .argnum = 2,
    .argname = {"options", "out"},
    .argtype = {ARG_INT, ARG_ADDRESS}};

PrepareTest(zx_port_create) { return &zx_port_create_test; }

struct function_description zx_port_queue_test = {
    .funcname = "zx_port_queue",
    .funcaddr = zx_port_queue,
    .argnum = 3,
    .argname = {"handle", "packet", "count"},
    .argtype = {ARG_ZX_HANDLE, ARG_ADDRESS, ARG_INT},
    .block = 1};

PrepareTest(zx_port_queue) { return &zx_port_queue_test; }

struct function_description zx_port_wait_test = {
    .funcname = "zx_port_wait",
    .funcaddr = zx_port_wait,
    .argnum = 4,
    .argname = {"handle", "deadline", "packet", "count"},
    .argtype = {ARG_ZX_HANDLE, ARG_INT64, ARG_ADDRESS, ARG_INT},
    .block = 1};

PrepareTest(zx_port_wait) { return &zx_port_wait_test; }

struct function_description zx_port_cancel_test = {
    .funcname = "zx_port_cancel",
    .funcaddr = zx_port_cancel,
    .argnum = 3,
    .argname = {"handle", "source", "key"},
    .argtype = {ARG_ZX_HANDLE, ARG_ZX_EVENT_HANDLE, ARG_INT64}};

PrepareTest(zx_port_cancel) { return &zx_port_cancel_test; }

static uint32_t ZX_CLOCK_FLAGS[] = {ZX_CLOCK_MONOTONIC, ZX_CLOCK_UTC,
                                    ZX_CLOCK_THREAD};

struct function_description zx_timer_create_test = {
    .funcname = "zx_timer_create",
    .funcaddr = zx_timer_create,
    .argnum = 3,
    .argname = {"options", "clock_id", "out"},
    .argtype = {ARG_INT, ARG_FLAGS, ARG_ADDRESS},
    .argselcnt[1] = ARRAY_SIZE(ZX_CLOCK_FLAGS),
    .argselect[1] = ZX_CLOCK_FLAGS,
};

PrepareTest(zx_timer_create) { return &zx_timer_create_test; }

struct function_description zx_timer_set_test = {
    .funcname = "zx_timer_set",
    .funcaddr = zx_timer_set,
    .argnum = 3,
    .argname = {"handle", "deadline", "slack"},
    .argtype = {ARG_ZX_TIMER_HANDLE, ARG_INT64, ARG_INT64}};

PrepareTest(zx_timer_set) { return &zx_timer_set_test; }

struct function_description zx_timer_cancel_test = {
    .funcname = "zx_timer_cancel",
    .funcaddr = zx_timer_cancel,
    .argnum = 1,
    .argname = {"handle"},
    .argtype = {ARG_ZX_TIMER_HANDLE}};

PrepareTest(zx_timer_cancel) { return &zx_timer_cancel_test; }

struct function_description zx_vmo_create_test = {
    .funcname = "zx_vmo_create",
    .funcaddr = zx_vmo_create,
    .argnum = 3,
    .argname = {"size", "options", "out"},
    .argtype = {ARG_INT64, ARG_INT, ARG_ADDRESS}};

PrepareTest(zx_vmo_create) { return &zx_vmo_create_test; }

struct function_description zx_vmo_read_test = {
    .funcname = "zx_vmo_read",
    .funcaddr = zx_vmo_read,
    .argnum = 4,
    .argname = {"handle", "data", "offset", "len"},
    .argtype = {ARG_ZX_VMO_HANDLE, ARG_ADDRESS, ARG_INT64, ARG_INT64},
    .argmax[0] = 4 * 4096,
};

PrepareTest(zx_vmo_read) { return &zx_vmo_read_test; }

struct function_description zx_vmo_write_test = {
    .funcname = "zx_vmo_write",
    .funcaddr = zx_vmo_write,
    .argnum = 4,
    .argname = {"handle", "data", "offset", "len"},
    .argtype = {ARG_ZX_VMO_HANDLE, ARG_ADDRESS, ARG_INT64, ARG_LEN},
    .argdep[3] = 1 + ARG_DEPEND_MIN,
};

PrepareTest(zx_vmo_write) { return &zx_vmo_write_test; }

struct function_description zx_vmo_get_size_test = {
    .funcname = "zx_vmo_get_size",
    .funcaddr = zx_vmo_get_size,
    .argnum = 2,
    .argname = {"handle", "size"},
    .argtype = {
        ARG_ZX_VMO_HANDLE,
        ARG_ADDRESS,
    }};

PrepareTest(zx_vmo_get_size) { return &zx_vmo_get_size_test; }

struct function_description zx_vmo_set_size_test = {
    .funcname = "zx_vmo_set_size",
    .funcaddr = zx_vmo_set_size,
    .argnum = 2,
    .argname = {"handle", "size"},
    .argtype = {
        ARG_ZX_VMO_HANDLE,
        ARG_INT,
    }};

PrepareTest(zx_vmo_set_size) { return &zx_vmo_set_size_test; }

static uint32_t ZX_VMO_OP_FLAGS[] = {
    ZX_VMO_OP_COMMIT,      ZX_VMO_OP_DECOMMIT,
    ZX_VMO_OP_LOCK,        ZX_VMO_OP_UNLOCK,
    ZX_VMO_OP_CACHE_SYNC,  ZX_VMO_OP_CACHE_INVALIDATE,
    ZX_VMO_OP_CACHE_CLEAN, ZX_VMO_OP_CACHE_CLEAN_INVALIDATE};

struct function_description zx_vmo_op_range_test = {
    .funcname = "zx_vmo_op_range",
    .funcaddr = zx_vmo_op_range,
    .argnum = 6,
    .argname = {"handle", "op", "offset", "size", "buffer", "buffer_size"},
    .argtype = {ARG_ZX_VMO_HANDLE, ARG_FLAGS, ARG_INT64, ARG_INT64, ARG_ADDRESS,
                ARG_LEN},
    .argselcnt[1] = ARRAY_SIZE(ZX_VMO_OP_FLAGS),
    .argselect[1] = ZX_VMO_OP_FLAGS,
    .argdep[5] = 4 + ARG_DEPEND_MIN,
};

PrepareTest(zx_vmo_op_range) { return &zx_vmo_op_range_test; }

static uint32_t ZX_VMO_CLONE_FLAGS[] = {ZX_VMO_CLONE_COPY_ON_WRITE};

struct function_description zx_vmo_clone_test = {
    .funcname = "zx_vmo_clone",
    .funcaddr = zx_vmo_clone,
    .argnum = 5,
    .argname = {"handle", "options", "offset", "size", "out"},
    .argtype = {ARG_ZX_VMO_HANDLE, ARG_FLAGS, ARG_INT64, ARG_INT64,
                ARG_ADDRESS},
    .argselcnt[1] = ARRAY_SIZE(ZX_VMO_CLONE_FLAGS),
    .argselect[1] = ZX_VMO_CLONE_FLAGS,
};

PrepareTest(zx_vmo_clone) { return &zx_vmo_clone_test; }

static uint32_t ZX_VMO_CACHE_POLICY_FLAGS[] = {
    ZX_CACHE_POLICY_CACHED, ZX_CACHE_POLICY_UNCACHED,
    ZX_CACHE_POLICY_UNCACHED_DEVICE, ZX_CACHE_POLICY_WRITE_COMBINING,
    ZX_CACHE_POLICY_MASK};

struct function_description zx_vmo_set_cache_policy_test = {
    .funcname = "zx_vmo_set_cache_policy",
    .funcaddr = zx_vmo_set_cache_policy,
    .argnum = 2,
    .argname = {"handle", "cache_policy"},
    .argtype = {ARG_ZX_VMO_HANDLE, ARG_FLAGS},
    .argselcnt[1] = ARRAY_SIZE(ZX_VMO_CACHE_POLICY_FLAGS),
    .argselect[1] = ZX_VMO_CACHE_POLICY_FLAGS,
};

PrepareTest(zx_vmo_set_cache_policy) { return &zx_vmo_set_cache_policy_test; }

static uint32_t ZX_VM_TEST_FLAGS[] = {
    ZX_VM_FLAG_PERM_READ,        ZX_VM_FLAG_PERM_WRITE,
    ZX_VM_FLAG_PERM_EXECUTE,     ZX_VM_FLAG_COMPACT,
    ZX_VM_FLAG_SPECIFIC,         ZX_VM_FLAG_SPECIFIC_OVERWRITE,
    ZX_VM_FLAG_CAN_MAP_SPECIFIC, ZX_VM_FLAG_CAN_MAP_READ,
    ZX_VM_FLAG_CAN_MAP_WRITE,    ZX_VM_FLAG_CAN_MAP_EXECUTE,
    ZX_VM_FLAG_MAP_RANGE};

struct function_description zx_vmar_allocate_test = {
    .funcname = "zx_vmar_allocate",
    .funcaddr = zx_vmar_allocate,
    .argnum = 6,
    .argname = {"parent_vmar_handle", "map_flags", "offset", "size",
                "child_vmar", "child_addr"},
    .argtype =
        {
            ARG_ZX_VMR_HANDLE,
            ARG_FLAGS,
            ARG_INT64,
            ARG_INT64,
            ARG_ADDRESS,
            ARG_ADDRESS,
        },
    .argmax[0] = 4 * 4096,
    .argmax[2] = 4 * 4096,
    .argmax[3] = 4 * 4096,
    .argselcnt[1] = ARRAY_SIZE(ZX_VM_TEST_FLAGS),
    .argselect[1] = ZX_VM_TEST_FLAGS,
};

PrepareTest(zx_vmar_allocate) { return &zx_vmar_allocate_test; }

struct function_description zx_vmar_destroy_test = {
    .funcname = "zx_vmar_destroy",
    .funcaddr = zx_vmar_destroy,
    .argnum = 1,
    .argname = {"vmar_handle"},
    .argtype = {ARG_ZX_VMR_HANDLE}};

PrepareTest(zx_vmar_destroy) { return &zx_vmar_destroy_test; }

struct function_description zx_vmar_map_test = {
    .funcname = "zx_vmar_map_fun",
    .funcaddr = zx_vmar_map_fun,
    .argnum = 5,
    .argname =
        {
            "vmar_handle",
            "map_flags",
            "offset",
            "vmo_handle",
            "vmo_offset",
            "len",
        },
    .argtype = {ARG_ZX_VMR_HANDLE, ARG_FLAGS, ARG_INT64, ARG_ZX_VMO_HANDLE,
                ARG_INT64, ARG_INT64},
    .argmax[0] = 4 * 4096,
    .argmax[2] = 4 * 4096,
    .argselcnt[1] = ARRAY_SIZE(ZX_VM_TEST_FLAGS),
    .argselect[1] = ZX_VM_TEST_FLAGS,
};

PrepareTest(zx_vmar_map_fun) { return &zx_vmar_map_test; }

struct function_description zx_vmar_unmap_test = {
    .funcname = "zx_vmar_unmap",
    .funcaddr = zx_vmar_unmap,
    .argnum = 3,
    .argname = {"vmar_handle", "addr", "len"},
    .argtype = {ARG_ZX_VMR_HANDLE, ARG_ADDRESS, ARG_INT64}};

PrepareTest(zx_vmar_unmap) { return &zx_vmar_unmap_test; }

struct function_description zx_vmar_protect_test = {
    .funcname = "zx_vmar_protect",
    .funcaddr = zx_vmar_protect,
    .argnum = 4,
    .argname = {"vmar_handle", "prot_flags", "addr", "len"},
    .argtype = {ARG_ZX_VMR_HANDLE, ARG_FLAGS, ARG_ADDRESS, ARG_LEN},
    .argselcnt[1] = ARRAY_SIZE(ZX_VM_TEST_FLAGS),
    .argselect[1] = ZX_VM_TEST_FLAGS,
    .argdep[3] = 2 + ARG_DEPEND_MIN,
};

PrepareTest(zx_vmar_protect) { return &zx_vmar_protect_test; }

struct function_description zx_fifo_create_test = {
    .funcname = "zx_fifo_create",
    .funcaddr = zx_fifo_create,
    .argnum = 5,
    .argname = {"elem_count", "elem_size", "options", "out0", "out1"},
    .argtype = {ARG_INT, ARG_INT, ARG_INT, ARG_ADDRESS, ARG_ADDRESS}};

PrepareTest(zx_fifo_create) { return &zx_fifo_create_test; }

struct function_description zx_fifo_read_test = {
    .funcname = "zx_fifo_read",
    .funcaddr = zx_fifo_read,
    .argnum = 4,
    .argname = {"handle", "data", "len", "num_written"},
    .argtype = {ARG_ZX_FIFO_HANDLE, ARG_ADDRESS, ARG_INT64, ARG_ADDRESS}};

PrepareTest(zx_fifo_read) { return &zx_fifo_read_test; }

struct function_description zx_fifo_read_old_test = {
    .funcname = "zx_fifo_read_old",
    .funcaddr = zx_fifo_read_old,
    .argnum = 4,
    .argname = {"handle", "data", "len", "num_written"},
    .argtype = {ARG_ZX_FIFO_HANDLE, ARG_ADDRESS, ARG_INT64, ARG_ADDRESS}};

PrepareTest(zx_fifo_read_old) { return &zx_fifo_read_old_test; }

struct function_description zx_fifo_write_test = {
    .funcname = "zx_fifo_write",
    .funcaddr = zx_fifo_write,
    .argnum = 4,
    .argname = {"handle", "data", "len", "num_written"},
    .argtype = {ARG_ZX_FIFO_HANDLE, ARG_ADDRESS, ARG_INT64, ARG_ADDRESS}};

PrepareTest(zx_fifo_write) { return &zx_fifo_write_test; }

struct function_description zx_fifo_write_old_test = {
    .funcname = "zx_fifo_write_old",
    .funcaddr = zx_fifo_write_old,
    .argnum = 4,
    .argname = {"handle", "data", "len", "num_written"},
    .argtype = {ARG_ZX_FIFO_HANDLE, ARG_ADDRESS, ARG_INT64, ARG_ADDRESS}};

PrepareTest(zx_fifo_write_old) { return &zx_fifo_write_old_test; }

struct function_description zx_profile_create_test = {
    .funcname = "zx_profile_create",
    .funcaddr = zx_profile_create,
    .argnum = 3,
    .argname = {"resource", "profile", "out"},
    .argtype = {ARG_ZX_HANDLE, ARG_ADDRESS, ARG_ADDRESS}};

PrepareTest(zx_profile_create) { return &zx_profile_create_test; }

struct function_description zx_vmar_unmap_handle_close_thread_exit_test = {
    .funcname = "zx_vmar_unmap_handle_close_thread_exit",
    .funcaddr = zx_vmar_unmap_handle_close_thread_exit,
    .argnum = 4,
    .argname = {"vmar_handle", "addr", "len", "handle"},
    .argtype = {ARG_ZX_VMR_HANDLE, ARG_ADDRESS, ARG_LEN, ARG_ZX_THREAD_HANDLE},
    .argdep[2] = 1 + ARG_DEPEND_MIN,
    .block = 1};

PrepareTest(zx_vmar_unmap_handle_close_thread_exit) {
  return &zx_vmar_unmap_handle_close_thread_exit_test;
}

struct function_description zx_futex_wake_handle_close_thread_exit_test = {
    .funcname = "zx_futex_wake_handle_close_thread_exit",
    .funcaddr = zx_futex_wake_handle_close_thread_exit,
    .argnum = 4,
    .argname = {"value_ptr", "count", "new_value", "handle"},
    .argtype = {ARG_ADDRESS, ARG_INT, ARG_INT, ARG_ZX_THREAD_HANDLE},
    .block = 1};

PrepareTest(zx_futex_wake_handle_close_thread_exit) {
  return &zx_futex_wake_handle_close_thread_exit_test;
}

struct function_description zx_log_create_test = {
    .funcname = "zx_log_create",
    .funcaddr = zx_log_create,
    .argnum = 2,
    .argname = {"options", "out"},
    .argtype = {ARG_INT, ARG_ADDRESS}};

PrepareTest(zx_log_create) { return &zx_log_create_test; }

struct function_description zx_log_write_test = {
    .funcname = "zx_log_write",
    .funcaddr = zx_log_write,
    .argnum = 4,
    .argname = {"handle", "len", "buffer", "options"},
    .argtype = {ARG_ZX_LOG_HANDLE, ARG_LEN, ARG_ADDRESS, ARG_INT}};

PrepareTest(zx_log_write) { return &zx_log_write_test; }

struct function_description zx_log_read_test = {
    .funcname = "zx_log_read",
    .funcaddr = zx_log_read,
    .argnum = 4,
    .argname = {"handle", "len", "buffer", "options"},
    .argtype = {ARG_ZX_LOG_HANDLE, ARG_LEN, ARG_ADDRESS, ARG_INT}};

PrepareTest(zx_log_read) { return &zx_log_read_test; }

struct function_description zx_debuglog_create_test = {
    .funcname = "zx_debuglog_create",
    .funcaddr = zx_debuglog_create,
    .argnum = 3,
    .argname = {"resource", "options", "out"},
    .argtype = {ARG_INT, ARG_INT, ARG_ADDRESS}};

PrepareTest(zx_debuglog_create) { return &zx_debuglog_create_test; }

struct function_description zx_debuglog_write_test = {
    .funcname = "zx_debuglog_write",
    .funcaddr = zx_debuglog_write,
    .argnum = 4,
    .argname = {"handle", "options", "buffer", "len"},
    .argtype = {ARG_ZX_DEBUGLOG_HANDLE, ARG_INT, ARG_ADDRESS, ARG_LEN}};

PrepareTest(zx_debuglog_write) { return &zx_debuglog_write_test; }

struct function_description zx_debuglog_read_test = {
    .funcname = "zx_debuglog_read",
    .funcaddr = zx_debuglog_read,
    .argnum = 4,
    .argname = {"handle", "options", "buffer", "len"},
    .argtype = {ARG_ZX_DEBUGLOG_HANDLE, ARG_INT, ARG_ADDRESS, ARG_LEN}};

PrepareTest(zx_debuglog_read) { return &zx_debuglog_read_test; }

struct function_description zx_trusty_set_config_test = {
    .funcname = "zx_trusty_set_config",
    .funcaddr = zx_trusty_set_config,
    .argnum = 5,
    .argname = {"process", "uuid", "uuid_len", "configs", "config_len"},
    .argtype = {ARG_ZX_PROCESS_HANDLE, ARG_ADDRESS, ARG_LEN, ARG_ADDRESS,
                ARG_LEN},
    .argdep[2] = 1 + ARG_DEPEND_MIN,
    .argdep[4] = 3 + ARG_DEPEND_MIN,
};

PrepareTest(zx_trusty_set_config) { return &zx_trusty_set_config_test; }
