#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "nsc_nss.h"

#include "libtar.h"

#define TEST_TAR "test.tar"

ssize_t _inner_read(int fd, void *buf, size_t count) {
  return nbl_nsc_read(fd, buf, count);
}
ssize_t _inner_write(int fd, const void *buf, size_t count) {
  return nbl_nsc_write(fd, buf, count);
}

/**
 * Put multiple files into TAR,
 * Paht of files must be different.
 */
void create_and_append() {
  int rlt;
  TAR *tar;

  char *buf;
  size_t buf_size;

  tartype_t tt = {nbl_nsc_open, nbl_nsc_close, _inner_read, _inner_write};

  rlt = tar_open(&tar, TEST_TAR, &tt, O_CREAT | O_WRONLY, 0644, 0);
  if (rlt == -1) {
    printf("tar_open failed, rlt=%d, errno=%d\n", rlt, errno);
    goto end;
  }

  rlt = tar_append_file(tar, "system/bin/run", "run");
  if (rlt == -1) {
    printf("tar_append_file failed, rlt=%d, errno=%d\n", rlt, errno);
    goto close;
  }

  buf = "hello world.\n";
  buf_size = strlen(buf);
  rlt = tar_append_buf(tar, "1.txt", (unsigned char *)buf, buf_size);
  if (rlt == -1) {
    printf("tar_append_file failed, rlt=%d, errno=%d\n", rlt, errno);
    goto close;
  }

  buf_size = 1029;
  buf = malloc(buf_size);
  for (int i = 0; i < buf_size; i++) buf[i] = i % 256;
  rlt = tar_append_buf(tar, "2/2.txt", (unsigned char *)buf, buf_size);
  if (rlt == -1) {
    printf("tar_append_buf failed, rlt=%d, errno=%d\n", rlt, errno);
    goto close;
  }

  rlt = tar_append_eof(tar);
  if (rlt == -1) {
    printf("tar_append_eof failed, rlt=%d, errno=%d\n", rlt, errno);
    goto close;
  }

close:
  tar_close(tar);

end:
  return;
}

void append_files_into_existed_tar() {
  int rlt;
  TAR *tar;

  tartype_t tt = {nbl_nsc_open, nbl_nsc_close, _inner_read, _inner_write};

  // If file exists, open it.
  // If not, return error.
  rlt = tar_open(&tar, TEST_TAR, &tt, O_RDWR, 0644, 0);
  if (rlt == -1) {
    // If file doesn't exist, create a new.
    printf("need to create, errno=%d\n", errno);
    rlt = tar_open(&tar, TEST_TAR, &tt, O_CREAT | O_WRONLY, 0644, 0);
    if (rlt == -1) {
      printf("tar_open create failed, rlt=%d, errno=%d\n", rlt, errno);
      goto end;
    }
  } else {
    // Travel to end of file, and append a new file into TAR.
    int i;
    while ((i = th_read(tar)) == 0) {
      th_print_long_ls(tar);
      if (TH_ISREG(tar) && tar_skip_regfile(tar) != 0) {
        fprintf(stderr, "tar_skip_regfile(): %s\n", strerror(errno));
        goto close;
      }
    }

    // seek backward 2 blocks for overwrite EOF.
    rlt = nbl_nsc_lseek(tar->fd, 0, SEEK_CUR);
    rlt = nbl_nsc_lseek(tar->fd, rlt - T_BLOCKSIZE * 2, SEEK_SET);
    if (rlt == -1) {
      printf("lseek failed, errno=%d\n", errno);
      goto close;
    }
  }

  size_t buf_size = 2029;
  char *buf = malloc(buf_size);
  for (int i = 0; i < buf_size; i++) buf[i] = i % 256;
  rlt = tar_append_buf(tar, "3/3.txt", (unsigned char *)buf, buf_size);
  if (rlt == -1) {
    printf("tar_append_buf failed, rlt=%d, errno=%d\n", rlt, errno);
    goto close;
  }

  rlt = tar_append_eof(tar);
  if (rlt == -1) {
    printf("tar_append_eof failed, rlt=%d, errno=%d\n", rlt, errno);
    goto close;
  }

close:
  tar_close(tar);

end:
  return;
}

int main(int argc, char *argv[]) {
  create_and_append();

  append_files_into_existed_tar();

  nbl_nsc_stop();

  return 0;
}
