// SPDX-License-Identifier: BSD-3-Clause

#ifndef GZFS_VSOCK_PROTO_H
#define GZFS_VSOCK_PROTO_H

#include <stdint.h>
#include <sys/stat.h>

#define MAX_PATH 1024
#define MAX_DATA 65536

typedef enum {
    OP_GETATTR = 1,
    OP_READDIR,
    OP_OPEN,
    OP_READ,
    OP_STATFS,
} opcode_t;

typedef struct {
    uint32_t opcode;
    uint32_t seq;
    uint32_t length;
} request_t;

typedef struct {
    uint32_t opcode;
    uint32_t seq;
    int32_t  result;
    uint32_t length;
} response_t;

typedef struct {
    char path[MAX_PATH];
} getattr_req_t;

typedef struct {
    struct stat st;
} getattr_resp_t;

typedef struct {
    char path[MAX_PATH];
} readdir_req_t;

typedef struct {
    uint64_t ino;
    uint32_t type;      // DT_REG, DT_DIR
    char name[256];
} dirent_t;

typedef struct {
    char path[MAX_PATH];
    int64_t offset;
    uint32_t size;
} read_req_t;

typedef struct {
    uint32_t size;
    char data[0];
} read_resp_t;

typedef struct {
    uint64_t blocks;
    uint64_t bfree;
    uint64_t bavail;
    uint64_t files;
    uint64_t ffree;
    uint32_t bsize;
    uint32_t namelen;
} statfs_t;

#endif