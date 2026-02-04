// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <fuz_common.h>

#include <trusty_std.h>
#include <uapi/err.h>

#include <service_utils.h>
#include <storage_utils.h>

static uint32_t STORAGE_OP_FLAGS[] = {
    STORAGE_MSG_FLAG_BATCH, STORAGE_MSG_FLAG_PRE_COMMIT,
    STORAGE_MSG_FLAG_POST_COMMIT, STORAGE_MSG_FLAG_TRANSACT_COMPLETE};

static uint32_t STORAGE_OPEN_FLAGS[] = {STORAGE_FILE_OPEN_CREATE,
                                        STORAGE_FILE_OPEN_CREATE_EXCLUSIVE,
                                        STORAGE_FILE_OPEN_TRUNCATE};

static uint32_t STORAGE_MOVE_FLAGS[] = {
    STORAGE_FILE_MOVE_CREATE,
    STORAGE_FILE_MOVE_CREATE_EXCLUSIVE,
    STORAGE_FILE_MOVE_OPEN_FILE,
};

static uint32_t STORAGE_READ_DIR_FLAGS[] = {
    STORAGE_FILE_LIST_START, STORAGE_FILE_LIST_END, STORAGE_FILE_LIST_COMMITTED,
    STORAGE_FILE_LIST_ADDED, STORAGE_FILE_LIST_REMOVED};

static uint32_t STORAGE_CMDS[] = {STORAGE_FILE_DELETE, STORAGE_FILE_OPEN,
                                  STORAGE_FILE_CLOSE, STORAGE_FILE_READ,
                                  STORAGE_FILE_WRITE, STORAGE_FILE_GET_SIZE,
                                  STORAGE_FILE_SET_SIZE,

                                  STORAGE_RPMB_SEND,

                                  /* transaction support */
                                  STORAGE_END_TRANSACTION,

                                  STORAGE_FILE_MOVE, STORAGE_FILE_LIST};

static void test_storage_random() {
  void *msg;
  uint32_t size;
  struct storage_msg *storage_msg;
  uint32_t cmd_inx;
  uint32_t cmd_size = ARRAY_SIZE(STORAGE_CMDS);

  generate_random_msg(&msg, &size);
  if (ONE_IN(10)) {
    send_random_msg(get_port_name());
  } else {
    storage_msg = (struct storage_msg *)msg;
    cmd_inx = fuz_rnd() % cmd_size;
    storage_msg->cmd = STORAGE_CMDS[cmd_inx];
  }
}
struct function_description storage_random_test = {
    .funcname = "storage_random",
    .funcaddr = test_storage_random,
    .argnum = 0,
};
PrepareTest(storage_random) { return &storage_random_test; }

static void test_storage_open_file(const char *name, uint32_t name_len,
                                   uint32_t flags, uint32_t opflags) {
  storage_session_t ss;
  file_handle_t file_handle;

  int rc = storage_open_session(&ss, get_port_name());
  if (rc < 0) {
    fuz_print_log("Error: %s %d, err: %d\n", __func__, __LINE__, rc);
    exit(-1);
  }

  storage_open_file(ss, &file_handle, name, name_len, flags, opflags);

  storage_delete_file(ss, name, name_len, STORAGE_OP_COMPLETE);
  storage_close_session(ss);
}
struct function_description storage_open_file_test = {
    .funcname = "storage_open_file",
    .funcaddr = test_storage_open_file,
    .argnum = 4,
    .argname = {"name", "name_len", "flags", "opflags"},
    .argtype = {ARG_STORAGE_FILE_NAME, ARG_STORAGE_FILE_NAME_LEN, ARG_FLAGS,
                ARG_FLAGS},
    .argdep[1] = 0 + ARG_DEPEND_MIN,
    .argselcnt[2] = ARRAY_SIZE(STORAGE_OPEN_FLAGS),
    .argselect[2] = STORAGE_OPEN_FLAGS,
    .argselcnt[3] = ARRAY_SIZE(STORAGE_OP_FLAGS),
    .argselect[3] = STORAGE_OP_FLAGS};
PrepareTest(storage_open_file) { return &storage_open_file_test; }

static void test_storage_delete_file(const char *name, uint32_t name_len,
                                     uint32_t opflags) {
  storage_session_t ss;
  file_handle_t file_handle;

  int rc = storage_open_session(&ss, get_port_name());
  if (rc < 0) {
    fuz_print_log("Error: %s %d, err: %d\n", __func__, __LINE__, rc);
    exit(-1);
  }

  if (RAND_BOOL()) {
    storage_open_file(ss, &file_handle, name, name_len,
                      STORAGE_FILE_OPEN_CREATE |
                          STORAGE_FILE_OPEN_CREATE_EXCLUSIVE,
                      STORAGE_OP_COMPLETE);
  }

  storage_delete_file(ss, name, name_len, opflags);
  storage_close_session(ss);
}
struct function_description storage_delete_file_test = {
    .funcname = "storage_delete_file",
    .funcaddr = test_storage_delete_file,
    .argnum = 4,
    .argname = {"name", "name_len", "opflags"},
    .argtype = {ARG_STORAGE_FILE_NAME, ARG_STORAGE_FILE_NAME_LEN, ARG_FLAGS},
    .argdep[1] = 0 + ARG_DEPEND_MIN,
    .argselcnt[2] = ARRAY_SIZE(STORAGE_OP_FLAGS),
    .argselect[2] = STORAGE_OP_FLAGS};
PrepareTest(storage_delete_file) { return &storage_delete_file_test; }

struct function_description storage_close_file_test = {
    .funcname = "storage_closee_file",
    .funcaddr = storage_close_file,
    .argnum = 1,
    .argname =
        {
            "file_handle",
        },
    .argtype = {
        ARG_STORAGE_FILE_HANDLE,
    }};
PrepareTest(storage_close_file) { return &storage_close_file_test; }

static void test_storage_move_file(const char *old_name, uint32_t old_len,
                                   const char *new_name, uint32_t new_len,
                                   uint32_t flag, uint32_t opflags) {
  storage_session_t ss;
  file_handle_t file_handle;
  uint32_t handle = 0;
  int rc;
  const char *port_name;

  port_name = get_port_name();
  rc = storage_open_session(&ss, port_name);
  if (rc < 0) {
    fuz_print_log("Error: %s %d, err: %d\n", __func__, __LINE__, rc);
    exit(-1);
  }

  if (RAND_BOOL()) {
    rc = storage_open_file(ss, &file_handle, old_name, old_len,
                           STORAGE_FILE_OPEN_CREATE, STORAGE_OP_COMPLETE);
    if (rc != NO_ERROR) {
      fuz_print_log("Error: %s %d, err: %d\n", __func__, __LINE__, rc);
    }
    register_fuz_storage_file(ss, (char *)old_name);
    handle = (uint32_t)file_handle;
    if (ONE_IN(10)) {
      handle = fuz_rnd();
    }
    if (RAND_BOOL()) {
      flag |= STORAGE_FILE_MOVE_OPEN_FILE;
    }
  }
  storage_move_file(ss, handle, old_name, old_len, new_name, new_len, flag,
                    opflags);

  storage_delete_file(ss, new_name, new_len, opflags);
  storage_close_session(ss);
}
struct function_description storage_move_file_test = {
    .funcname = "storage_move_file",
    .funcaddr = test_storage_move_file,
    .argnum = 4,
    .argname = {"old_name", "old_len", "new_name", "new_len", "flags",
                "opflags"},
    .argtype = {ARG_STORAGE_FILE_NAME, ARG_STORAGE_FILE_NAME_LEN,
                ARG_STORAGE_FILE_NAME, ARG_STORAGE_FILE_NAME_LEN, ARG_FLAGS,
                ARG_FLAGS},
    .argdep[1] = 0 + ARG_DEPEND_MIN,
    .argdep[3] = 2 + ARG_DEPEND_MIN,
    .argselcnt[4] = ARRAY_SIZE(STORAGE_MOVE_FLAGS),
    .argselect[4] = STORAGE_MOVE_FLAGS,
    .argselcnt[5] = ARRAY_SIZE(STORAGE_OP_FLAGS),
    .argselect[5] = STORAGE_OP_FLAGS};
PrepareTest(storage_move_file) { return &storage_move_file_test; }

static void test_storage_read_dir(void *arg) {
  (void)arg;
  uint32_t file_count;
  char **names;
  char *name;
  uint32_t name_len;
  storage_session_t ss;
  const char *port_name;
  file_handle_t handle;
  int rc;

  file_count = fuz_rnd() % 50 + 1;
  names = malloc(file_count * sizeof(char *));
  if (names == NULL) {
    fuz_print_log("Error: %s %d\n", __func__, __LINE__);
    exit(-1);
  }
  register_fuz_mem(names);

  // Create File Names.
  for (uint32_t i = 0; i < file_count; i++) {
    name_len = fuz_rnd() % (STORAGE_MAX_NAME_LEN) + 2;
    name = calloc(1, name_len);
    if (name == NULL) {
      fuz_print_log("Error: %s %d\n", __func__, __LINE__);
      exit(-1);
    }
    register_fuz_mem(name);
    fill_storage_name((uint8_t *)name, name_len - 1);
    *(names + i) = name;
  }

  port_name = get_port_name();
  rc = storage_open_session(&ss, port_name);
  if (rc != NO_ERROR) {
    fuz_print_log("Error: %s %d, err:%d\b", __func__, __LINE__, rc);
    exit(-1);
  }

  // Create Files.
  for (uint32_t i = 0; i < file_count; i++) {
    name = *(names + i);
    storage_open_file(ss, &handle, name, strlen(name), STORAGE_FILE_OPEN_CREATE,
                      RAND_BOOL() ? STORAGE_OP_COMPLETE : 0);
  }

  uint8_t flag_inx;
  uint32_t name_inx;
  uint32_t max;

  max = fuz_rnd() % (file_count + 10);
  flag_inx = fuz_rnd() / (ARRAY_SIZE(STORAGE_READ_DIR_FLAGS));
  if (ONE_IN(5)) {
    name = NULL;
  } else {
    if (ONE_IN(10)) {
      uint32_t size = fuz_rnd() % STORAGE_MAX_NAME_LEN + 1;
      name = malloc(size);
      if (name == NULL) {
        fuz_print_log("Error: %s %d\n", __func__, __LINE__);
        exit(-1);
      }
      register_fuz_mem(name);
      fill_storage_name((uint8_t *)name, size - 1);
    } else {
      name_inx = fuz_rnd() % file_count;
      name = *(names + name_inx);
    }
  }
  storage_read_dir(ss, STORAGE_READ_DIR_FLAGS[flag_inx], max, (char *)name);

  // Delete Files.
  for (uint32_t i = 0; i < file_count; i++) {
    name = *(names + i);
    storage_delete_file(ss, name, strlen(name), STORAGE_OP_COMPLETE);
  }

  storage_close_session(ss);
}
struct function_description storage_read_dir_test = {
    .argnum = 0,
    .do_custom_test = test_storage_read_dir,
};
PrepareTest(storage_read_dir) { return &storage_read_dir_test; }

struct function_description storage_read_test = {
    .funcname = "storage_read",
    .funcaddr = storage_read,
    .argnum = 4,
    .argname = {"file_handle", "off", "buf", "size"},
    .argtype = {ARG_STORAGE_FILE_HANDLE, ARG_INT, ARG_NORMAL_BUF,
                ARG_NORMAL_BUF_SIZE},
    .argmax[1] = STORAGE_WRITE_MAX,
    .argmax[2] = STORAGE_WRITE_MAX,
    .argdep[3] = 2 + ARG_DEPEND_MIN};
PrepareTest(storage_read) { return &storage_read_test; }

struct function_description storage_write_test = {
    .funcname = "_storage_write_",
    .funcaddr = _storage_write_,
    .argnum = 5,
    .argname = {"file_handle", "off", "buf", "size", "opflags"},
    .argtype = {ARG_STORAGE_FILE_HANDLE, ARG_INT, ARG_NORMAL_BUF,
                ARG_NORMAL_BUF_SIZE, ARG_FLAGS},
    .argmax[1] = STORAGE_WRITE_MAX,
    .argmax[2] = STORAGE_WRITE_MAX,
    .argdep[3] = 2 + ARG_DEPEND_MIN,
    .argselcnt[4] = ARRAY_SIZE(STORAGE_OP_FLAGS),
    .argselect[4] = STORAGE_OP_FLAGS};
PrepareTest(storage_write) { return &storage_write_test; }

struct function_description storage_set_file_size_test = {
    .funcname = "storage_set_file_size",
    .funcaddr = storage_set_file_size,
    .argnum = 3,
    .argname = {"file_handle", "file_size", "opflags"},
    .argtype = {ARG_STORAGE_FILE_HANDLE, ARG_INT, ARG_FLAGS},
    .argselcnt[2] = ARRAY_SIZE(STORAGE_OP_FLAGS),
    .argselect[2] = STORAGE_OP_FLAGS};
PrepareTest(storage_set_file_size) { return &storage_set_file_size_test; }

static void test_storage_get_file_size(file_handle_t fh) {
  storage_off_t size;

  storage_get_file_size(fh, &size);
}
struct function_description storage_get_file_size_test = {
    .funcname = "storage_get_file_size",
    .funcaddr = test_storage_get_file_size,
    .argnum = 1,
    .argname = {"file_handle"},
    .argtype = {ARG_STORAGE_FILE_HANDLE}};
PrepareTest(storage_get_file_size) { return &storage_get_file_size_test; }

static void test_storage_end_transaction(uint32_t flag) {
  uint32_t choose = fuz_rnd() % 3;
  const char *port_name = get_port_name();
  char file_name[24];
  storage_session_t session;
  file_handle_t file_handle;
  int rc;
  uint32_t opflag;
  size_t size;

  rc = storage_open_session(&session, port_name);
  if (rc != NO_ERROR) {
    fuz_print_log("Error: %s %d, err:%d\n", __func__, __LINE__, rc);
    exit(-1);
  }
  fill_storage_name((uint8_t *)file_name, 24);

  opflag = RAND_BOOL() ? 0 : STORAGE_OP_COMPLETE;
  rc = storage_open_file(session, &file_handle, file_name, 24,
                         STORAGE_FILE_OPEN_CREATE, opflag);
  if (rc != NO_ERROR) {
    fuz_print_log("Error: %s %d, err:%d\n", __func__, __LINE__, rc);
    exit(-1);
  }

  switch (choose) {
  case 0:
    break;
  case 1:
    // Write file
    opflag = RAND_BOOL() ? 0 : STORAGE_OP_COMPLETE;
    size = storage_write(file_handle, 0, file_name, 24, opflag);
    break;
  case 2:
    // Delete file
    opflag = RAND_BOOL() ? 0 : STORAGE_OP_COMPLETE;
    storage_delete_file(session, file_name, 24, opflag);
    break;
  }

  _storage_end_transaction_(session, flag);
}
struct function_description storage_end_transaction_test = {
    .funcname = "storage_end_transaction",
    .funcaddr = test_storage_end_transaction,
    .argnum = 1,
    .argname[0] = "flag",
    .argtype[0] = ARG_FLAGS,
    .argselcnt[0] = ARRAY_SIZE(STORAGE_OP_FLAGS),
    .argselect[0] = STORAGE_OP_FLAGS};
PrepareTest(storage_end_transaction) { return &storage_end_transaction_test; }

typedef struct storage_param {
  const char *port_name;
  char *file_name;
} storage_param_t;

static int test_concurrent_work(void *arg) {
  uint32_t opflag;
  storage_param_t *param = arg;
  const char *port_name = param->port_name;
  char *file_name = param->file_name;
  storage_session_t session;
  file_handle_t file_handle;
  int rc;

  rc = storage_open_session(&session, port_name);
  if (rc != NO_ERROR) {
    fuz_print_log("Error: %s %d, err:%d\n", __func__, __LINE__, rc);
    return -1;
  }

  opflag = RAND_BOOL() ? 0 : STORAGE_OP_COMPLETE;
  rc = storage_open_file(session, &file_handle, file_name, strlen(file_name),
                         STORAGE_FILE_OPEN_CREATE, opflag);
  if (rc != NO_ERROR) {
    fuz_print_log("Error: %s %d, err:%d\n", __func__, __LINE__, rc);
    return -1;
  }

  storage_write(file_handle, 0, file_name, strlen(file_name), opflag);
  storage_delete_file(session, file_name, strlen(file_name), opflag);
  storage_end_transaction(session, STORAGE_OP_COMPLETE);
  storage_close_session(session);

  return 0;
}

void test_storage_concurrent_random(void *arg) {
  (void)arg;
  uint8_t file_name1[24];
  uint8_t file_name2[24];
  const char *port_name1;
  const char *port_name2;
  storage_param_t param1;
  storage_param_t param2;
  thrd_t thrd1;
  thrd_t thrd2;
  int res;

  fill_storage_name(file_name1, 23);
  file_name1[23] = 0;
  fill_storage_name(file_name2, 23);
  file_name2[23] = 0;
  port_name1 = get_port_name();
  port_name2 = get_port_name();
  param1.file_name = (char *)file_name1;
  param1.port_name = port_name1;
  param2.file_name = (char *)file_name2;
  param2.port_name = port_name2;

  res = thrd_create(&thrd1, test_concurrent_work, &param1);
  if (res != 0) {
    fuz_print_log("Error: %s %d, err:%d\n", __func__, __LINE__, res);
    exit(-1);
  }
  res = thrd_create(&thrd2, test_concurrent_work,
                    RAND_BOOL() ? &param1 : &param2);
  if (res != 0) {
    fuz_print_log("Error: %s %d, err:%d\n", __func__, __LINE__, res);
    exit(-1);
  }

  thrd_join(thrd1, &res);
  thrd_join(thrd2, &res);
}
struct function_description storage_concurrent_random_test = {
    .funcname = "test_storage_concurrent_random",
    .funcaddr = test_storage_concurrent_random,
    .argnum = 0,
};
PrepareTest(storage_concurrent_random) {
  return &storage_concurrent_random_test;
}
