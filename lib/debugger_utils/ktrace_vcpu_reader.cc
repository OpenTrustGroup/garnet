// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Simple reader for ktrace vcpu files.

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string>

#include <zircon/ktrace.h>

#include "base64.h"
#include "minigzip.h"

namespace debugserver {
namespace ktrace {

typedef ktrace_header_t KtraceVcpuRecord;

static inline bool isVcpuEvent(KtraceVcpuRecord* rec) {
  unsigned evt = KTRACE_EVENT(rec->tag);
  return (KTRACE_GROUP(rec->tag) == KTRACE_GRP_VCPU)
      && (evt == 0x180 || evt == 0x181);
}

#define ONE_LOG_MAX_LEN  32
static int ProcessRecord(KtraceVcpuRecord* rec, int fd_out,
    unsigned long base_ts) {
  char log[ONE_LOG_MAX_LEN];
  int r;

  // TODO: Remove magic number
  switch (KTRACE_EVENT(rec->tag)) {
    case 0x180:
      r = snprintf(log, sizeof(log), "B%x%x%" PRIx64 ";",
              rec->tid & 0xf, (rec->tid >> 16) & 0xf,
              (long)rec->ts - base_ts);
      break;
    case 0x181:
      r = snprintf(log, sizeof(log), "E%x%x%" PRIx64 ";",
              rec->tid & 0xf, (rec->tid >> 16) & 0xf,
              (long)rec->ts - base_ts);
      break;
    default:
      // ignore this record
      return 0;
  }

  if (r < 0) {
    fprintf(stderr, "failed to fill log!\n");
    return r;
  }
  if ((unsigned long)r >= sizeof(log)) {
    fprintf(stderr, "log truncated!\n");
    // exclude the terminating NUL character
    r--;
  }

  if (write(fd_out, log, r) != r) {
    fprintf(stderr, "failed to write output file\n");
    return -1;
  }

  return 0;
}

static int ProcessRecords(int fd_int, KtraceVcpuRecord* buffer,
    size_t size, int fd_out, unsigned long* base_ts) {
  int ret = -1;
  char* s_rec = (char*)buffer;
  size_t handled = 0;

  for (;;) {
    KtraceVcpuRecord* rec = (KtraceVcpuRecord*)s_rec;
    uint32_t len = KTRACE_LEN(rec->tag);

    if (handled + len > size) {
      // read more to parse
      return size - handled;
    }
    // only interested in vcpu events
    if (isVcpuEvent(rec)) {
      if (*base_ts == 0) {
        char xfmt[18] = {'\0'};
        *base_ts = rec->ts;
        snprintf(xfmt, sizeof(xfmt), "%" PRIx64 "\n", *base_ts);
        write(fd_out, xfmt, strlen(xfmt));
      }
      ret = ProcessRecord(rec, fd_out, *base_ts);
      if (ret)
        return ret;
    }
    s_rec += len;
    handled += len;
    if (handled >= size)
      break;
  } 
  return 0;
}

#define KTRACE_HDR_BUF_CNT  (1024)
static KtraceVcpuRecord khbuffer[KTRACE_HDR_BUF_CNT];
void ProcessRecordsFile(int fd_in, int fd_out) {
  unsigned long base_ts = 0;
  size_t bufsz = sizeof(khbuffer);
  ssize_t retsz, offs;

  for (;;) {
    retsz = read(fd_in, khbuffer, bufsz);
    if (retsz < 0) {
      fprintf(stderr, "%s", strerror(errno));
      exit(1);
    } else if (retsz == 0)
      break;

    offs = ProcessRecords(fd_in, khbuffer, retsz, fd_out, &base_ts);
    if (offs < 0) {
      fprintf(stderr, "encounter error while processing record(%zd)\n", offs);
      exit(1);
    } else if (offs > 0)
      // fallback to last event's header position
      lseek(fd_in, -offs, SEEK_CUR);

    if ((size_t)retsz < bufsz) {
      if (offs > 0)
        fprintf(stderr, "probably the last one is incomplete record\n");
      break;
    }
  }
}

}  // namespace ktrace
}  // namespace debugserver

int main(int argc, char* argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: ktrace-vcpu-reader <device>\n");
    exit(1);
  }

  const char* file = argv[1];
  int fd_in = open(file, O_RDONLY);
  if (fd_in < 0) {
    fprintf(stderr, "Error opening: %s: %s\n",
        file, strerror(errno));
    exit(1);
  }

  const std::string kfile = "/data/ktrace";

  int fd_out = open(kfile.c_str(), O_CREAT | O_WRONLY);
  if (fd_out < 0) {
    fprintf(stderr, "Error opening: %s: %s\n",
        kfile.c_str(), strerror(errno));
    exit(1);
  }

  printf("ktrace-vcpu-reader: Parsing ktrace records, please wait......\n");
  debugserver::ktrace::ProcessRecordsFile(fd_in, fd_out);
  printf("ktrace-vcpu-reader: Parse ktrace records done!\n");

  close(fd_out);
  close(fd_in);

  printf("ktrace-vcpu-reader: Compressing ktrace, please wait......\n");
  char *gzfile = gz_file_compress((char*)kfile.c_str());
  printf("ktrace-vcpu-reader: Compress ktrace done!\n");

  printf("ktrace-vcpu-reader: base64 encoding, please wait......\n");
  base64_file_encode(gzfile);
  printf("ktrace-vcpu-reader: encode done!\n");

  free(gzfile);

  return 0;
}

