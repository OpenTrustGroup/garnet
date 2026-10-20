// SPDX-License-Identifier: BSD-3-Clause

#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <fbl/unique_free_ptr.h>
#include <fbl/unique_ptr.h>
#include <lib/async-loop/cpp/loop.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "garnet/lib/littlefs/littlefs.h"
#include "garnet/lib/littlefs/vnode.h"

using littlefs::LittleFs;

off_t get_size(int fd) {
  block_info_t info;
  if (ioctl_block_get_info(fd, &info) != sizeof(info)) {
    fprintf(stderr, "error: littlefs could not find size of device\n");
    return 0;
  }
  return info.block_size * info.block_count;
}

int do_littlefs_check(fbl::unique_ptr<littlefs::Bcache> bc) {
  return 0;
}

int do_littlefs_mount(fbl::unique_ptr<littlefs::Bcache> bc,
                      littlefs::options_t* options) {
  zx_handle_t h = zx_get_startup_handle(PA_HND(PA_USER0, 0));
  if (h == ZX_HANDLE_INVALID) {
    fprintf(stderr,
            "littlefs: Could not access startup handle to mount point\n");
    return EXIT_FAILURE;
  }

  std::unique_ptr<LittleFs> fs;
  zx_status_t status = LittleFs::Create(fbl::move(bc), &fs);
  if (status != ZX_OK) {
    return status;
  }

  async::Loop loop;
  if ((status =
           fs->MountAndServe(options, loop.async(), zx::channel(h)) != ZX_OK)) {
    return EXIT_FAILURE;
  }

  loop.Run();
  return 0;
}

int do_littlefs_mkfs(fbl::unique_ptr<littlefs::Bcache> bc) {
  std::unique_ptr<LittleFs> fs;
  auto status = LittleFs::Create(fbl::move(bc), &fs);
  if (status != ZX_OK) {
    return status;
  }

  if ((status = fs->Format() != ZX_OK)) {
    return EXIT_FAILURE;
  }
  return 0;
}

struct {
  const char* name;
  int (*func)(fbl::unique_ptr<littlefs::Bcache> bc);
  uint32_t flags;
  const char* help;
} CMDS[] = {
    {"create", do_littlefs_mkfs, O_RDWR | O_CREAT, "initialize filesystem"},
    {"mkfs", do_littlefs_mkfs, O_RDWR | O_CREAT, "initialize filesystem"},
    {"check", do_littlefs_check, O_RDONLY, "check filesystem integrity"},
    {"fsck", do_littlefs_check, O_RDONLY, "check filesystem integrity"},
};

int usage() {
  fprintf(stderr,
          "usage: littlefs [ <option>* ] <command> [ <arg>* ]\n"
          "\n"
          "options:  -v|--verbose     Some debug messages\n"
          "          -r|--readonly    Mount filesystem read-only\n"
          "          -h|--help        Display this message\n"
          "\n");
  for (unsigned n = 0; n < fbl::count_of(CMDS); n++) {
    fprintf(stderr, "%9s %-10s %s\n", n ? "" : "commands:", CMDS[n].name,
            CMDS[n].help);
  }
  fprintf(stderr, "%9s %-10s %s\n", "", "mount", "mount filesystem");
  fprintf(stderr, "\n");
  return -1;
}

int main(int argc, char** argv) {
  littlefs::options_t options;
  options.readonly = false;
  options.verbose = false;
  options.collect_metrics = false;

  while (1) {
    static struct option opts[] = {
        {"readonly", no_argument, nullptr, 'r'},
        {"verbose", no_argument, nullptr, 'v'},
        {"metrics", no_argument, nullptr, 'm'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };
    int opt_index;
    int c = getopt_long(argc, argv, "rvhm", opts, &opt_index);
    if (c < 0) {
      break;
    }
    switch (c) {
      case 'r':
        options.readonly = true;
        break;
      case 'v':
        options.verbose = true;
        break;
      case 'm':
        options.collect_metrics = true;
        break;
      case 'h':
      default:
        return usage();
    }
  }

  argc -= optind;
  argv += optind;

  // Block device passed by handle
  if (argc != 1) {
    return usage();
  }
  char* cmd = argv[0];

  fbl::unique_fd fd;
  fd.reset(FS_FD_BLOCKDEVICE);
  if (!options.readonly) {
    block_info_t block_info;
    zx_status_t status =
        static_cast<zx_status_t>(ioctl_block_get_info(fd.get(), &block_info));
    if (status < ZX_OK) {
      fprintf(stderr,
              "littlefs: Unable to query block device, fd: %d status: 0x%x\n",
              fd.get(), status);
      return -1;
    }
    options.readonly = block_info.flags & BLOCK_FLAG_READONLY;
  }

  off_t size = get_size(fd.get());
  if (size == 0) {
    fprintf(stderr, "littlefs: failed to access block device\n");
    return usage();
  }
  size /= littlefs::kBlockSize;

  fbl::unique_ptr<littlefs::Bcache> bc;
  if (littlefs::Bcache::Create(&bc, fbl::move(fd), (uint32_t)size) < 0) {
    fprintf(stderr, "littlefs: error: cannot create block cache\n");
    return EXIT_FAILURE;
  }

  if (!strcmp(cmd, "mount")) {
    return do_littlefs_mount(fbl::move(bc), &options);
  }

  for (unsigned i = 0; i < fbl::count_of(CMDS); i++) {
    if (!strcmp(cmd, CMDS[i].name)) {
      int r = CMDS[i].func(fbl::move(bc));
      if (options.verbose) {
        fprintf(stderr, "littlefs: %s completed with result: %d\n", cmd, r);
      }
      return r;
    }
  }

  return EXIT_FAILURE;
}