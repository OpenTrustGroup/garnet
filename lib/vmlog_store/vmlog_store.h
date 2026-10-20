// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "lib/fxl/logging.h"
#include "lib/fxl/synchronization/thread_annotations.h"

#include <fbl/auto_lock.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fbl/atomic.h>
#include <fbl/unique_ptr.h>
#include <zircon/types.h>
#include <lib/zx/log.h>

#include <thread>
#include <mutex>
#include <cstdio>
#include <atomic>

#define VMLOG_STORE_LINE_BUFFER_SIZE 200
#define VMLOG_STORE_MAX_FILE_BACKUPS 5
#define LOG_FILE_SYNC_PERIOD_SEC 3
#define LOG_FILE_SYNC_THRESHOLD_BYTES (4096)

enum LogStoreFlags : uint32_t {
  TO_SERIAL  = 1u << 0,
  TO_CONSOLE = 1u << 1,
  TO_FILE    = 1u << 2,
};

class VmlogStore {
 public:
  explicit VmlogStore();
  ~VmlogStore();

  zx_status_t Initialize(int32_t vmid, uint32_t flags = TO_SERIAL | TO_FILE);
  uint32_t GetFlags(void) { return flags_; }
  void SetFlags(uint32_t flags) { flags_ = flags;}
  void WriteLog(const char* log, size_t len);
  void Shutdown(void);

 private:
  void WriteDlogLocked(const char* log, int len, uint32_t dlog_flags)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(store_lock_);
  void WriteFileLocked(const char* log, int len)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(store_lock_);
  void CheckSizeAndRotateLocked()
      FXL_EXCLUSIVE_LOCKS_REQUIRED(store_lock_);
  zx_status_t LogFileRotate(std::string base_filename);
  void LogFileSyncLoop(void);
  bool is_file(const char* path);
  int64_t get_file_size(const char* path);
  void sync_file(FILE* fp);
  void close_file_safely(FILE* fp);
  FILE* open_log_file(const char* path);
  void rename_file_safe(const char* old_path, const char* new_path);
  void remove_file_safe(const char* path);

  std::mutex store_lock_;
  char log_with_prefix_[VMLOG_STORE_LINE_BUFFER_SIZE + 22];
  int32_t vmid_;
  std::string vm_name_;
  std::thread file_sync_thrd_;
  zx::log dlog_;
  FILE *log_fp_;
  std::string log_file_;
  uint32_t flags_;
  std::atomic<bool> shutdown_;
  fbl::atomic<uint64_t> dirty_bytes_counter_{0};
};
