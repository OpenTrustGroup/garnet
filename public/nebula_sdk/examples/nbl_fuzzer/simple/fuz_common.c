// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.

/**
 * Provide HELPER functions for fuzzing test.
 * Such as requiring/releasing resources.
 */

#include <limits.h>
#include <stdarg.h>
#include <string.h>

#include <zircon/nslist.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <signal.h>
#include <trusty_ipc.h>
#include <trusty_std.h>
#include <uapi/err.h>

#include "fuz_common.h"
#include "storage_utils.h"

typedef struct fuz_resource {
  uint32_t type;
  union {
    void *mem;
    char *file_name;
    uint32_t handle;
    uint64_t val;
    storage_session_t session;
  };
  void *private;
} fuz_resource;

// keep all memory should be free.
nslist_t g_resource_list = LIST_INITIAL_VALUE;

static void fuz_do_test_use_thread(function_description *description);
static void fuz_generate_args(function_description *description);
static void fuz_do_fuzzing(function_description *description);

static void free_resource(void *p);
void free_all_fuz_resource();

/////////////////////////////////////////////////
// NSS log.

void fuz_print_log(char *fmt, ...) {
  va_list args;

  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}

void fuz_print_buf(uint32_t type, char *name, void *addr, size_t size) {
  switch (type) {
  case BUF_TYPE_NULL:
    if (name) {
      fuz_print_log("%s=NULL, ", name);
    }
    break;
  case BUF_TYPE_INVALID:
    if (name) {
      fuz_print_log("%s=invalid pointer(%p), ", name, addr);
    }
    break;
  case BUF_TYPE_ADDR:
    if (name) {
      fuz_print_log("%s=malloc(size=%zu)%p, ", name, size, addr);
    }
    break;
  }
}

/////////////////////////////////////////////////

void register_fuz_mem(void *addr) {
  register_fuz_resource(RES_TYP_MEM, (uint64_t)addr, 0);
}

void register_fuz_trusty_handle(uint32_t handle) {
  register_fuz_resource(RES_TYP_TRUSTY_HANDLE, handle, 0);
}

void register_fuz_storage_file(storage_session_t ss, char *file_name) {
  char *fn = NULL;
  if (file_name != NULL) {
    fn = strdup(file_name);
  }
  register_fuz_resource(RES_TYP_STORAGE_FILE, ss, (void *)fn);
}

void register_fuz_zx_handle(uint32_t handle) {
  register_fuz_resource(RES_TYP_ZX_HANDLE, handle, 0);
}

/**
 * Get a value in [0~MAX).
 */
unsigned int fuz_get_uint_in_range(const char *name, unsigned int max_value) {
  int rlt;
  unsigned int data;

  rlt = draw_data_by_trusty_prng(sizeof(data), (uint8_t *)&data);
  if (rlt) {
    fuz_print_log("Cannot draw data by trusty prng.\n");
    exit(1);
  }

  unsigned int value = data % max_value;

  if (name) {
    fuz_print_log("%s=%u, ", name, value);
  }

  return value;
}

void fuz_fill_string_buffer(uint8_t *buf, size_t size) {
  if (size == 0) {
    return;
  }

  draw_data_by_trusty_prng(size, buf);
  if (RAND_BOOL()) {
    uint32_t not_zero_size = fuz_rnd() % (size + 1);
    uint32_t zero_size = size - not_zero_size;
    for (uint32_t i = 0; i < not_zero_size; i++) {
      buf[i] = buf[i] % 255 + 1;
    }
    if (zero_size > 0) {
      memset(buf + not_zero_size, 0, zero_size);
    }
  }
}

void fuz_fill_iov_struct(struct iovec *iov, uint32_t iov_cnt,
                         uint32_t max_buf_len) {
  size_t buf_len = max_buf_len;
  void *buf;
  uint32_t type;

  buf_len = max_buf_len;
  for (uint32_t i = 0; i < iov_cnt; i++) {
    type = fuz_get_output_buf(&buf, &buf_len);
    if (type == BUF_TYPE_ADDR && buf_len != 0) {
      draw_data_by_trusty_prng(buf_len, buf);
    }
    iov[i].base = buf;
    iov[i].len = buf_len;
    if (RAND_BOOL()) {
      iov[i].len = fuz_rnd();
    }
  }
}

/**
 * Get a buffer as a given length(bytes) is for input.
 * Content of buffer is NOT set.
 */
static int has_size(size_t *size) { return (size != NULL) && (*size); }

uint32_t fuz_get_output_buf(void **buf, size_t *size) {
  uint32_t r = fuz_get_uint_in_range(NULL, 10);
  if (r == 0) { // buffer is NULL.
    if (size != NULL) {
      *size = 0;
    }
    *buf = NULL;
    return BUF_TYPE_NULL;
  } else if (r < 3) { // Buffer is any invalid value.
    if (size != NULL) {
      *size = 0;
    }
    draw_data_by_trusty_prng(sizeof(void *), (uint8_t *)buf);
    return BUF_TYPE_INVALID;
  } else { // Buffer is valid buffer pointer, but size may  NOT be same to
           // argument "size".
    size_t real_size;
    if (ONE_IN(5) && has_size(size)) {
      real_size = *size;
    } else {
      draw_len_by_trusty_prng(&real_size);
      if (!ONE_IN(10) && has_size(size)) {
        real_size = real_size % (*size);
      }
    }
    *buf = malloc(real_size);
    if (size != NULL) {
      *size = real_size;
    }
    register_fuz_mem(*buf);

    return BUF_TYPE_ADDR;
  }
}

uint32_t fuz_get_output_string_buf(void **buf, size_t size) {
  char *str =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz,./"
      ";'<>?!@#$%^&*()+-,._-=[]{}|:`~";
  char *out_string = NULL;
  if (size == 0) {
    *buf = NULL;
    return BUF_TYPE_NULL;
  }
  *buf = malloc(size);
  if (*buf == NULL) {
    fuz_print_log("Failed to malloc buf.\n");
    *buf = NULL;
    return BUF_TYPE_NULL;
  }
  out_string = *buf;
  for (uint32_t i = 0; i < size - 1; i++) {
    int rd = rand() % (strlen(str));
    out_string[i] = str[rd];
  };
  if (size > 0) {
    out_string[size - 1] = '\0';
  }
  register_fuz_mem(*buf);

  return BUF_TYPE_ADDR;
}

void (*__func_call__)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                      uint64_t);
static void fuz_do_call(function_description *description) {
  __func_call__ = description->funcaddr;
  __func_call__(description->arg[0], description->arg[1], description->arg[2],
                description->arg[3], description->arg[4], description->arg[5]);
}

void fuz_do_test(fuz_seed_t rand_seed, function_description *description) {
  int retry_cnt = 0;

  fuz_init_random(rand_seed);
  fuz_print_log("%#lx==> ", rand_seed);

  if (!description->block) {
    fuz_do_fuzzing(description);
  } else {
    fuz_do_test_use_thread(description);
  }

  fuz_print_log("==>End.\n");
}

// Test fuzzing in main thread or new thread.
static void fuz_do_fuzzing(function_description *description) {
  // It should be in this function that allocate and release resources.
  if (description->do_setup) {
    description->do_setup(description);
  }

  fuz_generate_args(description);

  if (description->do_custom_test) {
    description->do_custom_test(description);
  } else {
    fuz_do_call(description);
  }

  free_all_fuz_resource();

  if (description->do_teardown) {
    description->do_teardown(description);
  }
}

uint32_t gettid(void);
static int fuzzing_thrd_entry(void *arg) {
  function_description *description = (function_description *)arg;

  description->thrd_handle = gettid();
  fuz_do_fuzzing(description);
  pthread_t timer_threadid = description->timer_threadid;
  if (timer_threadid) {
    pthread_kill(timer_threadid, SIGKILL);
  }
  return 0;
}

static void fuz_do_test_use_thread(function_description *description) {
  thrd_t thrd;
  int res;

  res = thrd_create(&thrd, fuzzing_thrd_entry, description);
  if (res != 0) {
    return;
  }
  thrd_join(thrd, NULL);
}

void register_fuz_resource(uint32_t type, uint64_t val, void *private) {
  fuz_resource *res;

  res = calloc(1, sizeof(fuz_resource));
  if (!res) {
    fuz_print_log("Error: failed to alloc resource.\n");
  }
  nslist_add_tail(&g_resource_list, res);
  res->val = val;
  res->type = type;
  res->private = private;
}

void free_all_fuz_resource() { nslist_clear(&g_resource_list, free_resource); }

static void free_resource(void *p) {
  fuz_resource *resource = (fuz_resource *)p;
  char *file_name;
  storage_session_t ss;

  switch (resource->type) {
  case RES_TYP_MEM:
    free(resource->mem);
    break;
  case RES_TYP_TRUSTY_HANDLE:
    _trusty_close(resource->handle);
    break;
  case RES_TYP_STORAGE_FILE:
    ss = resource->session;
    file_name = resource->private;
    _storage_delete_file_(ss, file_name);
    break;
  case RES_TYP_NSC_FILE_FD: {
    int fd = resource->session;
    file_name = resource->private;

    nsc_close_and_delete_file(fd, file_name);
    if (file_name)
      free(file_name);
  }
  case RES_TYP_ZX_HANDLE:
    zx_handle_close(resource->handle);
    break;
  }
  free(p);
}

static void fuz_fill_arg_addr(function_description *description,
                              uint32_t arg_inx) {
  void *addr;
  char *name = description->argname[arg_inx];
  uint32_t type;
  size_t size = description->argmax[arg_inx];

  type = fuz_get_output_buf((void **)&addr, &size);
  description->arg[arg_inx] = (uint64_t)addr;
  description->arglen[arg_inx] = size;

  fuz_print_buf(type, name, addr, size);
}

static void fuz_fill_arg_len(function_description *description,
                             uint32_t arg_inx) {
  int depend = description->argdep[arg_inx];
  int depend_inx = depend - ARG_DEPEND_MIN;

  if (depend >= ARG_DEPEND_MIN) {
    if (RAND_BOOL()) {
      description->arg[arg_inx] = description->arglen[depend_inx];
    } else {
      description->arg[arg_inx] = (size_t)fuz_get_len();
    }
  } else {
    description->arg[arg_inx] = (size_t)fuz_get_len();
  }

  if (description->argname[arg_inx]) {
    fuz_print_log("%s=%zu, ", description->argname[arg_inx],
                  (size_t)description->arg[arg_inx]);
  }
}

static void fuz_fill_arg_int(function_description *description,
                             uint32_t arg_inx) {
  uint32_t val;
  uint32_t max = description->argmax[arg_inx];

  if (RAND_BOOL()) {
    val = (uint32_t)fuz_get_uint();
  } else {
    val = (uint32_t)fuz_get_len();
  }

  if (max != 0) {
    if (!(ONE_IN(10))) {
      val = (val % (max + 1));
    }
  }

  description->arg[arg_inx] = val;
  if (description->argname[arg_inx]) {
    fuz_print_log("%s=%zu, ", description->argname[arg_inx],
                  (size_t)description->arg[arg_inx]);
  }
}

static void fuz_fill_arg_flags(function_description *description,
                               uint32_t arg_inx) {
  uint32_t flags_num = (uint32_t)description->argselcnt[arg_inx];
  uint32_t *flags = description->argselect[arg_inx];
  uint32_t flag = 0;
  char *name = description->argname[arg_inx];

  if (ONE_IN(10)) {
    flag = fuz_rnd();
  } else {
    flag = get_rand_bitmask(flags_num, flags);
  }

  description->arg[arg_inx] = flag;
  if (name) {
    fuz_print_log("%s=%x, ", name, flag);
  }
}

static void fuz_fill_arg_select(function_description *description,
                                uint32_t arg_inx) {
  uint32_t val;
  uint32_t val_cnt = description->argselcnt[arg_inx];
  uint32_t *vals = description->argselect[arg_inx];
  char *name = description->argname[arg_inx];

  if (ONE_IN(10)) {
    val = fuz_rnd();
    fuz_print_log("%s=(random)%zu ", name, val);
  } else {
    val = vals[fuz_rnd() % (val_cnt + 1)];
    fuz_print_log("%s=%zu, ", name, val);
  }

  description->arg[arg_inx] = val;
}

static void fuz_fill_arg_int64(function_description *description,
                               uint32_t arg_inx) {
  uint64_t val;
  uint32_t max = description->argmax[arg_inx];

  if (RAND_BOOL()) {
    val = fuz_get_uint64();
  } else {
    val = fuz_rand32();
  }

  if (max != 0) {
    if (RAND_BOOL()) {
      val = (val % (max + 1));
    }
  }

  description->arg[arg_inx] = val;
  if (description->argname[arg_inx]) {
    fuz_print_log("%s=%lu, ", description->argname[arg_inx],
                  (size_t)description->arg[arg_inx]);
  }
}

static void fuz_fill_arg_iov(function_description *description,
                             uint32_t arg_inx) {
  struct iovec *iovs;
  uint32_t iov_cnt;
  void *addr;
  uint32_t type;
  char *name = description->argname[arg_inx];
  size_t size = description->argmax[arg_inx];

  description->arglen[arg_inx] = 1;
  if (ONE_IN(5)) {
    type = fuz_get_output_buf(&addr, &size);
    if (type == BUF_TYPE_ADDR && size != 0) {
      draw_data_by_trusty_prng(size, addr);
    }
    fuz_print_buf(type, name, addr, size);
    description->arg[arg_inx] = (uint64_t)addr;
  } else {
    iov_cnt = fuz_rnd() % 16;
    if (iov_cnt == 0) {
      description->arg[arg_inx] = 0;
      fuz_print_log("%s=NULL", name);
      return;
    }
    iovs = malloc(sizeof(struct iovec) * iov_cnt);
    if (iovs == NULL) {
      fuz_print_log("Error:%s %d\n", __func__, __LINE__);
      exit(-1);
    }
    fuz_fill_iov_struct(iovs, iov_cnt, 4096);
    description->arg[arg_inx] = (uint64_t)iovs;
    description->arglen[arg_inx] = iov_cnt;
    fuz_print_log("%s=(iovec)%p,cnt=%d, ", name, iovs, iov_cnt);
  }
}

static void fuz_fill_arg_normal_buf(function_description *description,
                                    uint32_t arg_inx) {
  uint32_t max = description->argmax[arg_inx];
  uint32_t min = description->argmin[arg_inx];
  char *name = description->argname[arg_inx];
  void *buf;
  uint32_t size;

  if (min == max) {
    size = min;
  } else {
    size = fuz_rnd() % (max - min + 1) + min;
  }
  if (ONE_IN(10)) {
    size = 0;
  }
  buf = malloc(size);
  if (buf == NULL) {
    fuz_print_log("Error:%s %d\n", __func__, __LINE__);
    exit(-1);
  }
  register_fuz_mem(buf);
  description->arg[arg_inx] = (uint64_t)buf;
  description->arglen[arg_inx] = size;
  fuz_print_log("%s=%p, ", name, buf);
}

static void fuz_fill_arg_normal_buf_size(function_description *description,
                                         uint32_t arg_inx) {
  char *name = description->argname[arg_inx];
  uint32_t argdep = description->argdep[arg_inx];
  uint32_t argdep_inx = argdep - 1;
  uint32_t deplen;

  if (argdep < ARG_DEPEND_MIN) {
    fuz_print_log("Error:%s %d\n", __func__, __LINE__);
    exit(-1);
  }

  deplen = description->arglen[argdep_inx];
  description->arg[arg_inx] = deplen;
  fuz_print_log("%s=%u, ", name, deplen);
}

// fill buffer with file path.
// file Path must be a string, but file name or length MAYBE are NOT legal or
// too long.
void fuz_fill_arg_file_path(function_description *description,
                            uint32_t arg_inx) {
  uint32_t size;
  char *name = NULL;
  bool is_legal = false;
  const char *param_name = description->argname[arg_inx];

  if (ONE_IN(20)) {
    size = fuz_rnd() % (16 * 1024); // prevent OOM in TA.
  } else {
    size = fuz_rnd() % PATH_MAX; // 4096 Bytes.
  }

  if (size == 0) {
    is_legal = false;
    name = NULL;
    goto end;
  }

  is_legal = size > PATH_MAX ? false : true;
  name = (char *)calloc(1, size);
  if (name == NULL) {
    fuz_print_log("Error: %s %d\n", __func__, __LINE__);
    exit(-1);
  }
  register_fuz_mem(name);

  // fill path with file name one by one.
  // cur_size must less than size, last one should be '\0'.
  int cur_size = 0;
  while ((uint32_t)cur_size < size) {
    int fn_size = 0;
    int left_size = size - cur_size - 1;
    if (ONE_IN(50)) {
      if (left_size == 0) {
        break;
      } else {
        fn_size = fuz_rnd() % left_size;
      }
    } else {
      fn_size = fuz_rnd() % NAME_MAX; // =255 BYTES.
    }
    fn_size = MIN(fn_size, left_size);

    is_legal = is_legal && (fn_size < NAME_MAX);

    fill_storage_name((uint8_t *)name + cur_size, fn_size);
    cur_size += fn_size;
    // HERE maybe set char to last char, but again set '\0' below.
    name[cur_size] = '/';
    cur_size++;
  }
  name[size - 1] = '\0';

end:
  if (is_legal) {
    fuz_print_log("%s=(legal)%s, ", param_name, name);
  } else {
    fuz_print_log("%s=(illegal)%s, ", param_name, name);
  }

  description->arg[arg_inx] = (uint64_t)name;
  description->arglen[arg_inx] = name ? strlen(name) : 0;
}

///////////////////////////////////////////////////////////.

void fuz_fill_arg_port_name(function_description *description,
                            uint32_t arg_inx);
void fuz_fill_arg_trusty_handle(function_description *description,
                                uint32_t arg_inx);
void fuz_fill_arg_trusty_uevent(function_description *description,
                                uint32_t arg_inx);
void fuz_fill_arg_trusty_msg(function_description *description,
                             uint32_t arg_inx);
void fuz_fill_arg_storage_file_name(function_description *description,
                                    uint32_t arg_inx);
void fuz_fill_arg_storage_file_name_len(function_description *description,
                                        uint32_t arg_inx);
void fuz_fill_arg_storage_file_handle(function_description *description,
                                      uint32_t arg_inx);
void fuz_fill_arg_string(function_description *description, uint32_t arg_inx);
void fuz_fill_arg_file_path(function_description *description,
                            uint32_t arg_inx);
void fuz_fill_arg_nsc_file_fd(function_description *description,
                              uint32_t arg_inx);
void fuz_fill_arg_zx_handle(function_description *description,
                            uint32_t arg_inx);
void fuz_fill_arg_zx_handle_buf(function_description *description,
                                uint32_t arg_inx);
void fuz_fill_arg_zx_channel_handle(function_description *description,
                                    uint32_t arg_inx);
void fuz_fill_arg_zx_socket_handle(function_description *description,
                                   uint32_t arg_inx);
void fuz_fill_arg_zx_thread_handle(function_description *description,
                                   uint32_t arg_inx);
void fuz_fill_arg_zx_process_handle(function_description *description,
                                    uint32_t arg_inx);
void fuz_fill_arg_zx_job_handle(function_description *description,
                                uint32_t arg_inx);
void fuz_fill_arg_zx_event_handle(function_description *description,
                                  uint32_t arg_inx);
void fuz_fill_arg_zx_timer_handle(function_description *description,
                                  uint32_t arg_inx);
void fuz_fill_arg_zx_vmo_handle(function_description *description,
                                uint32_t arg_inx);
void fuz_fill_arg_zx_vmr_handle(function_description *description,
                                uint32_t arg_inx);
void fuz_fill_arg_zx_fifo_handle(function_description *description,
                                 uint32_t arg_inx);
void fuz_fill_arg_zx_log_handle(function_description *description,
                                uint32_t arg_inx);
void fuz_fill_arg_zx_debuglog_handle(function_description *description,
                                     uint32_t arg_inx);

static fuz_generate_arg generate_arg_functions[] = {
    fuz_fill_arg_addr,
    fuz_fill_arg_len,
    fuz_fill_arg_int,
    fuz_fill_arg_port_name,
    fuz_fill_arg_flags,
    fuz_fill_arg_trusty_handle,
    fuz_fill_arg_select,
    fuz_fill_arg_trusty_uevent,
    fuz_fill_arg_int64,
    fuz_fill_arg_iov,
    fuz_fill_arg_trusty_msg,
    fuz_fill_arg_storage_file_name,
    fuz_fill_arg_storage_file_name_len,
    fuz_fill_arg_storage_file_handle,
    fuz_fill_arg_normal_buf,
    fuz_fill_arg_normal_buf_size,
    fuz_fill_arg_string,
    fuz_fill_arg_file_path,
    fuz_fill_arg_nsc_file_fd,
    fuz_fill_arg_zx_handle,
    fuz_fill_arg_zx_handle_buf,
    fuz_fill_arg_zx_channel_handle,
    fuz_fill_arg_zx_socket_handle,
    fuz_fill_arg_zx_thread_handle,
    fuz_fill_arg_zx_process_handle,
    fuz_fill_arg_zx_job_handle,
    fuz_fill_arg_zx_event_handle,
    fuz_fill_arg_zx_timer_handle,
    fuz_fill_arg_zx_vmo_handle,
    fuz_fill_arg_zx_vmr_handle,
    fuz_fill_arg_zx_fifo_handle,
    fuz_fill_arg_zx_log_handle,
    fuz_fill_arg_zx_debuglog_handle,
};

static void fuz_generate_args(function_description *description) {
  enum argtype type;
  fuz_generate_arg func;

  if (description->argnum > ARG_CNT_MAX) {
    fuz_print_log("Error:while %s for %s, line:%d\n", __func__,
                  description->funcname, __LINE__);
    return;
  }

  for (uint32_t i = 0; i < description->argnum; i++) {
    type = description->argtype[i];
    if (type >= ARG_TYPE_MAX) {
      fuz_print_log("Error %s %d, funcname:%s, inx:%d\n", __func__, __LINE__,
                    description->funcname, i);
      exit(-1);
    }
    func = generate_arg_functions[type];
    if (func == NULL) {
      fuz_print_log("Error %s %d, funcname:%s, inx:%d\n", __func__, __LINE__,
                    description->funcname, i);
      exit(-1);
    }
    func(description, i);
  }

  if (description->do_adjust) {
    description->do_adjust(description);
  }
}
