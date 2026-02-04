/*
 * Copyright (C) 2015-2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <interface/storage/storage.h>
#include <lk/compiler.h>
#include <stdbool.h>
#include <stdint.h>
#include <trusty_ipc.h>

#define STORAGE_MAX_NAME_LENGTH_BYTES 159

#define STORAGE_MAX_NAME_LEN (128 + 64 - 33)

// case error:
// void transaction_complete(struct transaction *):268: transaction would leave
// fs too full, abort, reserved count=5115
//#define STORAGE_WRITE_MAX (1 * 1024 * 1024)
#define STORAGE_WRITE_MAX (8192)

__BEGIN_CDECLS

typedef handle_t storage_session_t;
typedef uint64_t file_handle_t;
typedef uint64_t storage_off_t;

struct storage_open_dir_state;

#define STORAGE_INVALID_SESSION ((storage_session_t)INVALID_IPC_HANDLE)

/**
 * storage_ops_flags - storage related operation flags
 * @STORAGE_OP_COMPLETE: forces to commit current transaction
 */
enum storage_ops_flags {
  STORAGE_OP_COMPLETE = 0x1,
};

const char* get_port_name();
void fill_storage_name(uint8_t* name, uint32_t size);
int storage_open_session(storage_session_t* session_p, const char* type);
void storage_close_session(storage_session_t session);
int storage_open_file(storage_session_t session,
                      file_handle_t* handle_p,
                      const char* name,
                      size_t name_len,
                      int32_t flags,
                      uint32_t opflags);
int storage_delete_file(storage_session_t session,
                        const char* name,
                        size_t name_len,
                        uint32_t opflags);
void storage_close_file(file_handle_t handle);
int storage_move_file(storage_session_t session,
                      file_handle_t handle,
                      const char* old_name,
                      uint32_t old_len,
                      const char* new_name,
                      uint32_t new_len,
                      uint32_t flags,
                      uint32_t opflags);
int storage_read_dir(storage_session_t session,
                     uint32_t flag,
                     uint32_t max,
                     char* name);
ssize_t storage_read(file_handle_t handle,
                     storage_off_t off,
                     void* buf,
                     size_t size);
ssize_t _storage_write_(file_handle_t handle,
                        storage_off_t off,
                        const void* buf,
                        size_t size,
                        uint32_t opflags);
int storage_set_file_size(file_handle_t handle,
                          storage_off_t file_size,
                          uint32_t opflags);
int storage_get_file_size(file_handle_t handle, storage_off_t* size);
int _storage_end_transaction_(storage_session_t session, uint32_t flag);

/**
 * storage_write() - Writes to a file at a given offset. Grows the file if
 * necessary.
 * @handle: the file_handle_t retrieved from storage_open_file
 * @off: the start offset from whence to write in the file
 * @buf: the buffer containing the data to write
 * @size: the size of buf and number of bytes to write
 * @opflags: a combination of @storage_op_flags
 *
 * Return: the number of bytes written on success, negative error code on
 * failure
 */
ssize_t storage_write(file_handle_t handle,
                      storage_off_t off,
                      const void* buf,
                      size_t size,
                      uint32_t opflags);
int storage_end_transaction(storage_session_t session, bool complete);

// add by GRT.
void _storage_delete_file_(storage_session_t ss, const char* file_name);

__END_CDECLS
