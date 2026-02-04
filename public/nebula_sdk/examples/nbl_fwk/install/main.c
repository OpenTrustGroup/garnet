// Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <nbl_fwk_ree/err.h>
#include <nbl_fwk_ree/loader.h>
#include <nbl_fwk_ree/secmgr.h>

static void help(const char* exe) {
  printf("%s <data dir>\n", exe);
}

static int install_file_by_secmgr(const char* dir, const char* file_name) {
  int fd = -1;
  int rc;
  nbl_otrp_cmd_t* otrp = NULL;
  nbl_otrp_resp_t* out_resp = NULL;
  char buf[1024 * 10];

  // open file.
  memset(buf, 0, sizeof(buf));
  sprintf(buf, "%s/%s", dir, file_name);
  fd = open(buf, O_RDONLY);
  if (fd < 0) {
    printf("Cannot open file \"%s\"\n", buf);
    return -1;
  }

  struct stat statbuf;
  rc = fstat(fd, &statbuf);
  if (rc < 0) {
    goto end;
  }

  // Connect to TEE and prepare stream to TEE.
  otrp = nbl_otrp_prepare_cmd(statbuf.st_size);
  if (!otrp) {
    rc = -1;
    printf("Cannot prepare otrp\n");
    goto end;
  }

  do {
    rc = read(fd, buf, sizeof(buf));
    if (rc < 0) {
      printf("Cannot read data from file. errno=%d", errno);
      goto end;
    } else if (rc == 0) {
      break;
    }

    // send data to TEE.
    rc = nbl_otrp_route_data(otrp, buf, rc);
    if (rc != NBL_NO_ERROR) {
      printf("Cannot route data into otrp. rc=%d\n", rc);
      goto end;
    }
  } while (1);

  // finish send.
  nbl_otrp_commit_cmd(otrp, &out_resp);
  otrp = NULL;
  // free response data.
  nbl_otrp_free_resp(out_resp);

end:
  if (fd != -1)
    close(fd);
  if (otrp) {
    nbl_otrp_cancel_cmd(otrp);
  }
  return rc;
}

int main(int argc, char* argv[]) {
  int rc;

  if (argc < 2) {
    help(argv[0]);
    exit(1);
  }

  // Install three provision files into TEE.

  rc = install_file_by_secmgr(argv[1], "provision_get_device_state.json");
  if (rc != 0) {
    return 1;
  }

  rc = install_file_by_secmgr(argv[1], "provision_create_sd.json");
  if (rc != 0) {
    return 1;
  }

  rc = install_file_by_secmgr(argv[1], "provision_install_ha_test-lib.json");
  if (rc != 0) {
    return 1;
  }

  // invoke this program : examples/hee/libc
  nbl_uuid_t uuid = {0xf3573a54,
                     0x4af5,
                     0x4d89,
                     {0xa6, 0xf2, 0x11, 0xd3, 0x6a, 0xa4, 0x99, 0xd5}};
  nbl_load_app_by_uuid(0, &uuid, NULL);

  return 0;
}
