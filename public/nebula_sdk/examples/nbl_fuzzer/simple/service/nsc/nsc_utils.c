// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.
#include <fuz_common.h>

#include <limits.h>

#include <nbl_fwk/log.h>
#include <trusty_std.h>
#include <uapi/err.h>

#include <service_utils.h>

#include <nsc_nss.h>
#include <nsc_time.h>

#include "service/nsc/nsc_utils.h"

#define LOG_TAG "nsc_fuz"

const char *nss_srv_port_name = "com.grt.nebula.nsc.srv";

uint32_t OPEN_FLAGS[18] = {
    O_CREAT,    O_EXCL,  O_TRUNC,  O_DIRECTORY, O_APPEND,   O_PATH,
    O_NONBLOCK, O_DSYNC, O_SYNC,   O_RSYNC,     O_NOFOLLOW, O_CLOEXEC,
    O_NOCTTY,   O_ASYNC, O_DIRECT, O_LARGEFILE, O_NOATIME,  O_TMPFILE};

uint32_t OPEN_MODES[9] = {
    S_IRUSR, S_IWUSR, S_IXUSR, S_IRGRP, S_IWGRP,
    S_IXGRP, S_IROTH, S_IWOTH, S_IXOTH,
};

uint32_t SEEK_FLAGS[3] = {
    SEEK_SET,
    SEEK_CUR,
    SEEK_END,
};

////////////////////////////////////////////////////////////

void register_fuz_nsc_file_fd(int fd, const char *file_name) {

  char *fn = NULL;
  if (file_name != NULL) {
    fn = strdup(file_name);
  }
  register_fuz_resource(RES_TYP_NSC_FILE_FD, fd, (void *)fn);
}

void nsc_close_and_delete_file(int fd, const char *file_name) {
  nbl_nsc_close(fd);
  nbl_nsc_unlink(file_name);
}

// fd of NSC. Invalid/Valid file with data and read/write privilege.
void fuz_fill_arg_nsc_file_fd(function_description *description,
                              uint32_t arg_inx) {
  int rc;
  const char *param_name = description->argname[arg_inx];
  int file_handle;
  char name[NAME_MAX];
  void *buf = NULL;
  uint32_t size;

  // Precondition is "nbl start".

  if (ONE_IN(3)) {
    file_handle = fuz_rnd();
    // register_fuz_nsc_file_fd(-1, NULL); NO NEED to regist and clean.
    fuz_print_log("%s=(random)%lx, ", param_name, file_handle);
  } else {
    memset(name, 0, sizeof(name));
    size_t name_len = fuz_rnd() % (sizeof(name) - 1);

    fill_storage_name((uint8_t *)name, name_len);

    int flags = O_CREAT | O_RDWR;

    int mode = S_IRWXU;

    rc = nbl_nsc_open(name, flags, mode);
    file_handle = rc;
    if (rc < 0) {
      fuz_print_log("%s=(failed)%d, ", param_name, file_handle);
      // NO NEED to regist and clean.
      goto end;
    }
    register_fuz_nsc_file_fd(file_handle, name);

    size = fuz_rnd() % (4 * 1024);
    buf = malloc(size);
    if (buf == NULL) {
      fuz_print_log("Error: %s %d.\n", __func__, __LINE__);
      exit(-1);
    }
    // data of buffer is any char.

    rc = nbl_nsc_write(file_handle, buf, size);
    if (rc < 0) {
      fuz_print_log("Error: %s %d.\n", __func__, __LINE__);
      // ignore error.
    }

    off_t offset;
    if (ONE_IN(10)) {
      offset = fuz_rnd();
    } else {
      offset = fuz_rnd() % size;
    }
    rc = nbl_nsc_lseek(file_handle, offset, SEEK_SET);
    if (rc < 0) {
      fuz_print_log("Error: %s %d.\n", __func__, __LINE__);
      // ignore error.
    }

    fuz_print_log("%s=%lx(fn=%s,flag=%#x,mode=%#x), ", param_name, file_handle,
                  name, flags, mode);
  }

end:
  if (buf)
    free(buf);
  description->arg[arg_inx] = file_handle;
}

////////////////////////////////////////////////////////////

int nbl_nsc_get_client_name() {
  int rc;

  nbl_chn_t *chn = NULL;

  // 1. Connect to HEE NSS srv.
  char *name = NULL;

  rc = nbl_chn_open(nss_srv_port_name, NBL_TIMEOUT_INFINITY, &chn);
  if (rc != NBL_NO_ERROR) {
    NBL_LOGE("Connect to %s failed (%d).\n", nss_srv_port_name, rc);
    goto conn_failed;
  }

  // 2. Send request to srv.
  nbl_stm_t *tx = nbl_stm_create();
  nbl_stm_t *rx = nbl_stm_create();

  nbl_stm_add_int(tx, NBL_NSC_CMD_START_CLIENT);

  rc = nbl_chn_write_stm(chn, tx);
  if (rc != NBL_NO_ERROR) {
    NBL_LOGE("Write stream failed (%d).\n", rc);
    goto failed;
  }

  // 3. Get client name.
  rc = nbl_chn_read_stm(chn, rx, NBL_TIMEOUT_INFINITY);
  if (rc != NBL_NO_ERROR) {
    NBL_LOGE("Read stream failed (%d).\n", rc);
    goto failed;
  }

  int name_size;
  void *name_ptr;

  // 3.1 Get name.
  rc = nbl_stm_next_buf_ptr(rx, &name_size, &name_ptr);
  if (rc != NBL_NO_ERROR) {
    NBL_LOGE("Cannot read client name 1. err=%d\n", rc);
    goto failed;
  }
  name = strdup(name_ptr);

  // 4. Clear env.
failed:
  if (name) {
    free(name);
  }

  nbl_stm_destroy(tx);
  nbl_stm_destroy(rx);

  if (chn) {
    nbl_chn_close(chn);
  }

conn_failed:
  return rc;
}

// send "finish start" message to server, and server will make somebody to
// connect this port.
int nbl_nsc_send_finish_start_port() {
  int rc;
  nbl_chn_t *chn = NULL;

  rc = nbl_chn_open(nss_srv_port_name, NBL_TIMEOUT_INFINITY, &chn);
  if (rc != NBL_NO_ERROR) {
    NBL_LOGE("Connect to %s failed (%d).\n", nss_srv_port_name, rc);
    goto conn_failed;
  }

  nbl_stm_t *tx = nbl_stm_create();
  nbl_stm_t *rx = nbl_stm_create();

  nbl_stm_add_int(tx, NBL_NSC_CMD_FINISH_START_CLIENT);

  rc = nbl_chn_write_stm(chn, tx);
  if (rc != NBL_NO_ERROR) {
    NBL_LOGE("Write stream failed (%d).\n", rc);
  }

  rc = nbl_chn_read_stm(chn, rx, NBL_TIMEOUT_INFINITY);
  if (rc != NBL_NO_ERROR) {
    NBL_LOGE("Read stream failed (%d).\n", rc);
    goto failed;
  }

  nbl_stm_next_int(rx, &rc);

failed:
  nbl_stm_destroy(tx);
  nbl_stm_destroy(rx);

  if (chn) {
    nbl_chn_close(chn);
  }

conn_failed:
  return rc;
}
