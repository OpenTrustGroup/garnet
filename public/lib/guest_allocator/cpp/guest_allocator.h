// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>

__BEGIN_CDECLS

#define STRING_SIZE 128

typedef struct mem_req {
    union {
        char path[STRING_SIZE];
        char compatible[STRING_SIZE];
        char search_string[STRING_SIZE];
    };
} mem_req_t;

typedef struct mem_resp {
    uint64_t size;
    uint64_t addr;
} mem_resp_t;

#define MAX_DTB_SIZE 600*1024

typedef struct mem_resp_dtb {
    uint64_t size;
} mem_resp_dtb_t;

#define IOCTL_GET_GUEST_MEMORY \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_NEBULA, 1)

#define IOCTL_GET_SYSTEM_MEMORY \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_NEBULA, 2)

#define IOCTL_GET_RESERVED_MEMORY \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_NEBULA, 3)

#define IOCTL_GET_GUEST_MEMORY_WITH_PATH \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_NEBULA, 4)

#define IOCTL_QUERY_PROP_WITH_PATH \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_NEBULA, 5)

#define IOCTL_GET_GUEST_DTB_MEMORY \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_NEBULA, 6)

// ssize_t ioctl_get_guest_memory(int fd, const mem_req_t* in, mem_resp_t* out);
IOCTL_WRAPPER_INOUT(ioctl_get_guest_memory, IOCTL_GET_GUEST_MEMORY, \
        mem_req_t, mem_resp_t);

// ssize_t ioctl_get_system_memory(int fd, mem_resp_t* out);
IOCTL_WRAPPER_OUT(ioctl_get_system_memory, IOCTL_GET_SYSTEM_MEMORY, \
        mem_resp_t);

// ssize_t ioctl_get_reserved_memory(int fd, const mem_req_t* in, mem_resp_t* out);
IOCTL_WRAPPER_INOUT(ioctl_get_reserved_memory, IOCTL_GET_RESERVED_MEMORY, \
        mem_req_t, mem_resp_t);

// ssize_t ioctl_get_guest_memory_with_path(int fd, const mem_req_t* in, mem_resp_t* out);
IOCTL_WRAPPER_INOUT(ioctl_get_guest_memory_with_path,
                    IOCTL_GET_GUEST_MEMORY_WITH_PATH,
                    mem_req_t,
                    mem_resp_t);

// ssize_t ioctl_query_prop_with_path(int fd, const mem_req_t* in, uint64_t *out_prop);
IOCTL_WRAPPER_INOUT(ioctl_query_prop_with_path,
                    IOCTL_QUERY_PROP_WITH_PATH,
                    mem_req_t,
                    uint64_t);

// ssize_t ioctl_get_guest_dtb_memory(int fd, zx_handle_t* out);
IOCTL_WRAPPER_INOUT(ioctl_get_guest_dtb_memory,
                    IOCTL_GET_GUEST_DTB_MEMORY,
                    mem_req_t,
                    zx_handle_t);
__END_CDECLS
