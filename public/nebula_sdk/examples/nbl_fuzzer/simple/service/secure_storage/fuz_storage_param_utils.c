#include <uapi/err.h>

#include <fuz_common.h>

#include <storage_utils.h>

uint8_t special_character[] = {'.', '-', '_'};

// fill name with character "[a-z][A-Z][0-9][.-_]",
// and size is "size", don't include '\0'.
void fill_storage_name(uint8_t *name, uint32_t size) {
  for (uint32_t i = 0; i < size; i++) {
    switch (fuz_rnd() % 4) {
    case 0:
      name[i] = 'a' + fuz_rnd() % 26;
      break;
    case 1:
      name[i] = 'A' + fuz_rnd() % 26;
      break;
    case 2:
      name[i] = '0' + fuz_rnd() % 10;
      break;
    case 3:
      name[i] = special_character[fuz_rnd() % 3];
      break;
    }
  }
}
void fuz_fill_arg_storage_file_name(function_description *description,
                                    uint32_t arg_inx) {
  uint32_t size;
  void *name;
  const char *param_name = description->argname[arg_inx];

  if (ONE_IN(10)) {
    size = fuz_rnd() % 1024;
  } else {
    size = fuz_rnd() % STORAGE_MAX_NAME_LEN;
  }

  name = calloc(1, size);
  if (name == NULL) {
    fuz_print_log("Error: %s %d\n", __func__, __LINE__);
    exit(-1);
  }
  register_fuz_mem(name);

  if (ONE_IN(10)) {
    fuz_fill_string_buffer(name, size);
    fuz_print_log("%s=(random)%p, ", param_name, name);
  } else {
    fill_storage_name(name, size);
    fuz_print_log("%s=(legal)%p, ", param_name, name, name);
  }
  description->arg[arg_inx] = (uint64_t)name;
  description->arglen[arg_inx] = size;
}

void fuz_fill_arg_storage_file_name_len(function_description *description,
                                        uint32_t arg_inx) {
  uint32_t arg_dep = description->argdep[arg_inx];
  uint32_t arg_dep_inx = arg_dep - ARG_DEPEND_MIN;
  const char *param_name = description->argname[arg_inx];
  uint32_t val;

  if (arg_dep <= 0) {
    fuz_print_log("Error: %s %d\n", __func__, __LINE__);
  }

  val = description->arglen[arg_dep_inx];

  description->arg[arg_inx] = val;
  fuz_print_log("%s=%zu, ", param_name, val);
}

static int get_file_handle_name(function_description *description,
                                uint32_t arg_inx, char **name);

void fuz_fill_arg_storage_file_handle(function_description *description,
                                      uint32_t arg_inx) {
  const char *param_name = description->argname[arg_inx];
  uint32_t handle;
  file_handle_t file_handle;
  char name[24];
  char *file_name;
  const char *port_name;
  storage_session_t ss;
  void *buf;
  uint32_t size;

  port_name = get_port_name();
  int rc = storage_open_session(&ss, port_name);
  if (rc < 0) {
    fuz_print_log("Error: %s %d, err: %d\n", __func__, __LINE__, rc);
    exit(-1);
  }
  register_fuz_trusty_handle(ss);
  if (ONE_IN(3)) {
    handle = fuz_rnd();
    file_handle = ((uint64_t)ss << 32) | handle;
    register_fuz_storage_file(ss, NULL);
    fuz_print_log("%s=(random)%lx, ", param_name, file_handle);
  } else {
    rc = get_file_handle_name(description, arg_inx, &file_name);
    if (rc != NO_ERROR) {
      snprintf(name, 23, "test_fuzzing_%08x", fuz_rnd());
      file_name = name;
    }
    rc = storage_open_file(ss, &file_handle, file_name, strlen(file_name),
                           STORAGE_FILE_OPEN_CREATE, STORAGE_OP_COMPLETE);
    if (rc < 0) {
      fuz_print_log("Error: %s %d, err: %d\n", __func__, __LINE__, rc);
      exit(-1);
    }
    register_fuz_storage_file(ss, file_name);

    size = fuz_rnd() % (STORAGE_WRITE_MAX + 1) + 1;
    buf = malloc(size);
    if (buf == NULL) {
      fuz_print_log("Error: %s %d.\n", __func__, __LINE__);
      exit(-1);
    }
    register_fuz_mem(buf);
    rc = storage_write(file_handle, 0, buf, size, STORAGE_OP_COMPLETE);
    if (rc <= 0) {
      fuz_print_log("Error: %s %d.\n", __func__, __LINE__);
      exit(-1);
    }
    fuz_print_log("%s=%lx, ", param_name, file_handle);
  }

  description->arg[arg_inx] = file_handle;
}

static int get_file_handle_name(function_description *description,
                                uint32_t arg_inx, char **name) {
  uint32_t argdep = description->argdep[arg_inx];
  uint32_t argdep_inx = argdep - ARG_DEPEND_MIN;

  if (argdep < ARG_DEPEND_MIN) {
    return ERR_NOT_FOUND;
  }

  *name = (char *)description->arg[argdep_inx];
  return NO_ERROR;
}
