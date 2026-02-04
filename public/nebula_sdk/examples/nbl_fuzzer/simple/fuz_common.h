// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.

/**
 * Common test type, macro and function.
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <zircon/threads.h>

#include <trusty_ipc.h>

////////////////////////////////////////////////////////////
// common macro and type.

#define MIN(x, y) ((x) > (y) ? (y) : (x))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define PRINT_TO_NSS 0

#define ARG_CNT_MAX 6
#define ARG_DEPEND_MIN 1

#define PAGE_SIZE 4096
#define PAGE_MASK (~(PAGE_SIZE - 1))

#define BUF_TYPE_NULL 0
#define BUF_TYPE_INVALID 1
#define BUF_TYPE_ADDR 2

#define MAX_BUF_SIZE (1 * 1024 * 1024 + 1) // TODO: max size is 1 MB.

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;

typedef unsigned long fuz_seed_t;

// session of secure storage.
typedef handle_t storage_session_t;

////////////////////////////////////////////////////////////
// Test and register.

// Type of resource for releasing.
#define RES_TYP_MEM 0
#define RES_TYP_TRUSTY_HANDLE 1
#define RES_TYP_STORAGE_FILE 2
#define RES_TYP_NSC_FILE_FD 3
#define RES_TYP_ZX_HANDLE 4

// Define customized argument type.
// How to create argument instance by functions defined in
// generate_arg_functions of fuz_commond.c Index of generate_arg_functions is
// same to argtype value.
enum argtype {
  // Created buffer and size <= argmax, NULL, random and invalid address.
  ARG_ADDRESS,
  // Relative buffer's size, or random length.
  ARG_LEN,
  // 9/10 = random int, 1/10 = <argmax.
  ARG_INT,
  // 9/10 = random string, 1/10 = any of known port names.
  ARG_PORT_NAME,
  // 1/10 = random data, 9/10 = mix value of argselect by OR.
  ARG_FLAGS,
  // 1/10 = random data, 9/10 = any valid handle of some types except special
  // types.
  ARG_TRUSTY_HANDLE,
  // 1/10 = random data, 9/10 = one of argselect.
  ARG_SELECT,
  // 1/5 = random data, 4/5 = valid uevent.
  ARG_TRUSTY_UEVENT,
  // random data of int64.
  ARG_INT64,
  // 1/5 = random data, 4/5 = empty or valid iovec data.
  ARG_IOV,
  // 1/5 = random data, 4/5 = valid msg;
  ARG_TRUSTY_MSG,
  // invalid and valid file name, size is any.
  ARG_STORAGE_FILE_NAME,
  // Relative size of file name.
  ARG_STORAGE_FILE_NAME_LEN,
  // Invalid or valid file handle. (Valid file is written some data).
  ARG_STORAGE_FILE_HANDLE,
  // created buffer, size <= argmax, or be malloc(0).
  ARG_NORMAL_BUF,
  // value is relative buffer's size.
  ARG_NORMAL_BUF_SIZE,
  // NULL/Invalid/Valid string, length maybe is NOT equal real length event on
  // valid string. buffer and size <= argmax or buffer = NULL
  ARG_TYPE_STRING,
  // NULL/Invalid/Valid path, but MUST be a string. Length is equal real length.
  ARG_FILE_PATH,
  // fd of NSC. Invalid/Valid file with data and read/write privilege.
  ARG_NSC_FILE_FD,
  ARG_ZX_HANDLE,
  ARG_ZX_HANDLE_BUF,
  ARG_ZX_CHANNEL_HANDLE,
  ARG_ZX_SOCKET_HANDLE,
  ARG_ZX_THREAD_HANDLE,
  ARG_ZX_PROCESS_HANDLE,
  ARG_ZX_JOB_HANDLE,
  ARG_ZX_EVENT_HANDLE,
  ARG_ZX_TIMER_HANDLE,
  ARG_ZX_VMO_HANDLE,
  ARG_ZX_VMR_HANDLE,
  ARG_ZX_FIFO_HANDLE,
  ARG_ZX_LOG_HANDLE,
  ARG_ZX_DEBUGLOG_HANDLE,
  // -------- Add enum above ------------
  ARG_TYPE_MAX
};

typedef void (*custom_test_func)(void *);
typedef void (*adjust_param)(void *);
typedef struct function_description {
  char *funcname;
  void *funcaddr;

  uint32_t argnum;
  char *argname[ARG_CNT_MAX];
  enum argtype argtype[ARG_CNT_MAX];
  uint32_t argmax[ARG_CNT_MAX];   // parameter max length or max value.
  uint32_t argmin[ARG_CNT_MAX];   // parameter max length or min value.
                                  // (TYPE=NORMAL BUFFER)
  uint8_t argdep[ARG_CNT_MAX];    // dependent parameter index + ARG_DEPEND_MIN.
  uint8_t argselcnt[ARG_CNT_MAX]; // count of parameter value range.
  uint32_t *argselect[ARG_CNT_MAX]; // parameter value range.
  uint64_t arg[ARG_CNT_MAX];        // parameter value.

  uint8_t block; // using for blocking functions.

  custom_test_func do_setup;       // call before fuzzing.
  custom_test_func do_teardown;    // call after fuzzing.
  custom_test_func do_custom_test; // write the test function by hand.
  adjust_param do_adjust; // ajust the parameter of function for special usage.

  // Do not need to be set manually.
  uint8_t argsgened;
  zx_handle_t thrd_handle;
  uint32_t arglen[ARG_CNT_MAX];
  pthread_t timer_threadid;
  uint32_t time_out; // Set test function timeout(millisecond)
  // User can set some data by this pointer.
  // Allocate and release by self.
  // void * user_data;
} function_description;

typedef function_description *(*prepare_func)();
void register_func(prepare_func func, const char *name);

typedef void (*fuz_generate_arg)(function_description *, uint32_t);

#define PrepareTest(name)                                                      \
  static function_description *prepare_func_##name();                          \
  __attribute__((constructor)) void _register_##name(void) {                   \
    register_func(prepare_func_##name, #name);                                 \
  }                                                                            \
  static function_description *prepare_func_##name()

void fuz_do_test(fuz_seed_t rand_seed, function_description *record);

void register_fuz_resource(uint32_t type, uint64_t val, void *private);

////////////////////////////////////////////////////////////
// Aux functions.

void register_fuz_mem(void *addr);
void register_fuz_trusty_handle(uint32_t handle);
void register_fuz_storage_file(storage_session_t ss, char *file_name);
void register_fuz_zx_handle(uint32_t handle);
void register_fuz_zx_handle_buf(uint32_t *handle);

unsigned int fuz_get_uint_in_range(const char *name, unsigned int max_value);
unsigned int fuz_get_uint();
uint64_t fuz_get_uint64();
uint32_t fuz_get_output_buf(void **buf, size_t *size);
uint32_t fuz_get_output_string_buf(void **buf, size_t size);

void fuz_fill_string_buffer(uint8_t *buf, size_t size);
void fuz_fill_iov_struct(struct iovec *iov, uint32_t iov_cnt,
                         uint32_t max_buf_len);
void *fuz_fill_fwk_msg(size_t *size);

void fuz_print_log(char *fmt, ...);
void fuz_print_buf(uint32_t type, char *name, void *addr, size_t size);

// fill name with size="size", not including "\0".
void fill_storage_name(uint8_t *name, uint32_t size);

////////////////////////////////////////////////////////////
// Random and interesting number
uint32_t fuz_rnd(void);

#define ONE_IN(x) ((fuz_rnd() % x) == 0)

#define RAND_BOOL() (fuz_rnd() & 1)
#define RAND_BYTE() (fuz_rnd() & 0xff)
#define RAND_RANGE(min, max)                                                   \
  (min + fuz_rnd() / (RAND_MAX / (max - min + 1) + 1))

int fuz_init_random(fuz_seed_t seed);
int fuz_draw_random(void *buf, size_t len);

int draw_data_by_trusty_prng(size_t len_bytes, uint8_t *bytes);
// Draw a random length(bytes) for random data.
int draw_len_by_trusty_prng(size_t *len_bytes);

uint32_t get_rand_bitmask(unsigned int num, uint32_t *values);

unsigned long fuz_rand_single_bit(unsigned char size);
unsigned short fuz_rand16(void);
unsigned int fuz_rand32(void);
unsigned long fuz_get_len(void);
unsigned int fuz_rnd_range(unsigned int max);

unsigned long get_interesting_value(void);

////////////////////////////////////////////////////////////
// other:
void nsc_close_and_delete_file(int fd, const char *file_name);
