// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../../../zircon/system/ulib/gpt/include/gpt/gpt.h"
#include <zircon/types.h>
#include <fdio/io.h>
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zircon/device/block.h>
#include <zircon/device/device.h>
#include "../../../zircon/system/ulib/pretty/include/pretty/hexdump.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define DEV_BLOCK "/dev/class/block"

struct guid {
  uint32_t data1;
  uint16_t data2;
  uint16_t data3;
  uint8_t data4[8];
};

void _uint8_to_guid_string(char *dst, const uint8_t *src) {
  struct guid *guid = (struct guid *)src;
  sprintf(dst, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", guid->data1,
          guid->data2, guid->data3, guid->data4[0], guid->data4[1],
          guid->data4[2], guid->data4[3], guid->data4[4], guid->data4[5],
          guid->data4[6], guid->data4[7]);
}

void _hexdump8_ex(const void *ptr, size_t len, uint64_t disp_addr) {
  uintptr_t address = (uintptr_t)ptr;
  size_t count = 0;
  size_t i;
  const size_t kMaxLineLen = 16;

  for (count = 0; count < len; count += 16) {
    size_t line_len = MIN(len - count, kMaxLineLen);

    printf(((disp_addr + len) > 0xFFFFFFFF) ? "0x%016" PRIx64 ": "
                                            : "0x%08" PRIx64 ": ",
           disp_addr + count);

    for (i = 0; i < line_len; i++) {
      printf("%02hhx ", *(const uint8_t *)(address + count + i));
    }

    for (; i < 16; i++) {
      printf("   ");
    }

    printf("|");

    for (i = 0; i < line_len; i++) {
      char c = ((const char *)address)[count + i];
      printf("%c", isprint((unsigned char)c) ? c : '.');
    }

    printf("\n");
  }
}
static char *size_to_cstring(char *str, size_t maxlen, uint64_t size) {
  const char *unit;
  uint64_t div;
  if (size < 1024) {
    unit = "";
    div = 1;
  } else if (size >= 1024 && size < 1024 * 1024) {
    unit = "K";
    div = 1024;
  } else if (size >= 1024 * 1024 && size < 1024 * 1024 * 1024) {
    unit = "M";
    div = 1024 * 1024;
  } else if (size >= 1024 * 1024 * 1024 &&
             size < 1024llu * 1024 * 1024 * 1024) {
    unit = "G";
    div = 1024 * 1024 * 1024;
  } else {
    unit = "T";
    div = 1024llu * 1024 * 1024 * 1024;
  }
  snprintf(str, maxlen, "%" PRIu64 "%s", size / div, unit);
  return str;
}

static const char *guid_to_type(char *guid) {
  if (!strcmp("FE3A2A5D-4F32-41A7-B725-ACCC3285A309", guid)) {
    return "cros kernel";
  } else if (!strcmp("3CB8E202-3B7E-47DD-8A3C-7FF2A13CFCEC", guid)) {
    return "cros rootfs";
  } else if (!strcmp("2E0A753D-9E48-43B0-8337-B15192CB1B5E", guid)) {
    return "cros reserved";
  } else if (!strcmp("CAB6E88E-ABF3-4102-A07A-D4BB9BE3C1D3", guid)) {
    return "cros firmware";
  } else if (!strcmp("C12A7328-F81F-11D2-BA4B-00A0C93EC93B", guid)) {
    return "efi system";
  } else if (!strcmp("EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", guid)) {
    return "data";
  } else if (!strcmp("21686148-6449-6E6F-744E-656564454649", guid)) {
    return "bios";
  } else if (!strcmp(GUID_SYSTEM_STRING, guid)) {
    return "fuchsia-system";
  } else if (!strcmp(GUID_DATA_STRING, guid)) {
    return "fuchsia-data";
  } else if (!strcmp(GUID_INSTALL_STRING, guid)) {
    return "fuchsia-install";
  } else if (!strcmp(GUID_BLOB_STRING, guid)) {
    return "fuchsia-blob";
  } else if (!strcmp(GUID_FVM_STRING, guid)) {
    return "fuchsia-fvm";
  } else {
    return "unknown";
  }
}

typedef struct blkinfo {
  char path[128];
  char topo[1024];
  char guid[GPT_GUID_STRLEN];
  char label[40];
  char sizestr[6];
} blkinfo_t;

static int cmd_list_blk(void) {
  struct dirent *de;
  DIR *dir = opendir(DEV_BLOCK);
  if (!dir) {
    printf("Error opening %s\n", DEV_BLOCK);
    return -1;
  }
  blkinfo_t info;
  const char *type;
  int fd;
  printf("%-3s %-4s %-14s %-20s %-6s %s\n", "ID", "SIZE", "TYPE", "LABEL",
         "FLAGS", "DEVICE");
  while ((de = readdir(dir)) != NULL) {
    if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
      continue;
    }
    memset(&info, 0, sizeof(blkinfo_t));
    type = NULL;
    snprintf(info.path, sizeof(info.path), "%s/%s", DEV_BLOCK, de->d_name);
    fd = open(info.path, O_RDONLY);
    if (fd < 0) {
      printf("Error opening %s\n", info.path);
      goto devdone;
    }
    if (ioctl_device_get_topo_path(fd, info.topo, sizeof(info.topo)) < 0) {
      strcpy(info.topo, "UNKNOWN");
    }

    block_info_t block_info;
    if (ioctl_block_get_info(fd, &block_info) > 0) {
      size_to_cstring(info.sizestr, sizeof(info.sizestr),
                      block_info.block_size * block_info.block_count);
    }
    uint8_t guid[GPT_GUID_LEN];
    if (ioctl_block_get_type_guid(fd, guid, sizeof(guid)) >= 0) {
      _uint8_to_guid_string(info.guid, guid);
      type = guid_to_type(info.guid);
    }
    ioctl_block_get_name(fd, info.label, sizeof(info.label));

    char flags[20] = {0};

    if (block_info.flags & BLOCK_FLAG_READONLY) {
      strlcat(flags, "RO ", sizeof(flags));
    }
    if (block_info.flags & BLOCK_FLAG_REMOVABLE) {
      strlcat(flags, "RE ", sizeof(flags));
    }
  devdone:
    close(fd);
    printf("%-3s %4s %-14s %-20s %-6s %s\n", de->d_name, info.sizestr,
           type ? type : "", info.label, flags, info.topo);
  }
  closedir(dir);
  return 0;
}

static int cmd_read_blk(const char *dev, off_t offset, size_t count) {
  int fd = open(dev, O_RDONLY);
  if (fd < 0) {
    printf("Error opening %s\n", dev);
    return fd;
  }

  // check that count and offset are aligned to block size
  uint64_t blksize;
  block_info_t info;
  ssize_t rc = ioctl_block_get_info(fd, &info);
  if (rc < 0) {
    printf("Error getting block size for %s\n", dev);
    close(fd);
    goto out;
  }
  blksize = info.block_size;
  if (count % blksize) {
    printf("Bytes read must be a multiple of blksize=%" PRIu64 "\n", blksize);
    rc = -1;
    goto out;
  }
  if (offset % blksize) {
    printf("Offset must be a multiple of blksize=%" PRIu64 "\n", blksize);
    rc = -1;
    goto out;
  }

  // read the data
  void *buf = malloc(count);
  if (offset) {
    rc = lseek(fd, offset, SEEK_SET);
    if (rc < 0) {
      printf("Error %zd seeking to offset %jd\n", rc, (intmax_t)offset);
      goto out2;
    }
  }
  ssize_t c = read(fd, buf, count);
  if (c < 0) {
    printf("Error %zd in read()\n", c);
    rc = c;
    goto out2;
  }

  _hexdump8_ex(buf, c, offset);

out2:
  free(buf);
out:
  close(fd);
  return rc;
}

static int cmd_stats(const char *dev, bool clear) {
  int fd = open(dev, O_RDONLY);
  if (fd < 0) {
    printf("Error opening %s\n", dev);
    return fd;
  }

  block_stats_t stats;
  ssize_t rc = ioctl_block_get_stats(fd, &clear, &stats);
  if (rc < 0) {
    printf("Error getting stats for %s\n", dev);
    close(fd);
    goto out;
  }

  printf("max concurrent ops:        %zu\n", stats.max_concur);
  printf("max pending block ops:     %zu\n", stats.max_pending);
  printf("total submitted block ops: %zu\n", stats.total_ops);
  printf("total submitted blocks:    %zu\n", stats.total_blocks);
out:
  close(fd);
  return rc;
}

int main(int argc, const char **argv) {
  int rc = 0;
  const char *cmd = argc > 1 ? argv[1] : NULL;
  if (cmd) {
    if (!strcmp(cmd, "help")) {
      goto usage;
    } else if (!strcmp(cmd, "read")) {
      if (argc < 5)
        goto usage;
      rc = cmd_read_blk(argv[2], strtoul(argv[3], NULL, 10),
                        strtoull(argv[4], NULL, 10));
    } else if (!strcmp(cmd, "stats")) {
      if (argc < 4)
        goto usage;
      if (strcmp("true", argv[3]) && strcmp("false", argv[3]))
        goto usage;
      rc = cmd_stats(argv[2], !strcmp("true", argv[3]) ? true : false);
    } else {
      printf("Unrecognized command %s!\n", cmd);
      goto usage;
    }
  } else {
    rc = cmd_list_blk();
  }
  return rc;
usage:
  printf("Usage:\n");
  printf("%s\n", argv[0]);
  printf("%s read <blkdev> <offset> <count>\n", argv[0]);
  printf("%s stats <blkdev> <clear=true|false>\n", argv[0]);
  return 0;
}
