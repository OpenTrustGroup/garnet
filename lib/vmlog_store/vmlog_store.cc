// SPDX-License-Identifier: BSD-3-Clause

#include <map>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#include <cstdio>

#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

#include <lib/fxl/strings/string_printf.h>
#include <lib/fxl/logging.h>

#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/file.h"
#include "garnet/lib/vm_id/vm_id.h"
#include "vmlog_store.h"

namespace {
// TODO: investigate rename log file issue under /data/vmlog sub directory
// constexpr char kVmlogStorageDir[] = "/data/vmlog";
constexpr char kVmlogStorageDir[] = "/data";
constexpr uint32_t kVmlogMaxFileBackups = 5;
constexpr size_t kLogFilenameMaxLen = 256;
constexpr uint64_t kLogFileMaximumAllowedSize = 2 * 1024 * 1024;

// TODO: read vm name table from config file
// format: {vmid, vm_name}
std::map<int32_t, std::string> vm_names = {
    {machina::kSosVmid, "yocto"},
    {machina::kAlpsVmid, "alps"},
    {machina::kTboxVmid, "tbox"},
};

std::string ErrnoString(int err) {
  return fxl::StringPrintf("%s(%d)", strerror(err), err);
}

} // namespace

bool VmlogStore::is_file(const char* path) {
  struct stat buf;
  if (stat(path, &buf) != 0)
    return false;
  return S_ISREG(buf.st_mode);
}

int64_t VmlogStore::get_file_size(const char* path) {
  struct stat buf;
  if (stat(path, &buf) != 0)
    return -1;
  if (!S_ISREG(buf.st_mode))
    return -1;
  return static_cast<int64_t>(buf.st_size);
}

void VmlogStore::sync_file(FILE* fp) {
  if (!fp) return;
  fflush(fp);
  if (fsync(fileno(fp)) != 0) {
    FXL_LOG(WARNING) << "vmlog file fsync error: " << ErrnoString(errno);
  }
}

void VmlogStore::close_file_safely(FILE* fp) {
  if (!fp) return;
  sync_file(fp);
  fclose(fp);
}

FILE* VmlogStore::open_log_file(const char* path) {
  FILE* fp = fopen(path, "w+");
  if (!fp) {
    FXL_LOG(ERROR) << "Could not open file: " << path << ", " << ErrnoString(errno);
    return nullptr;
  }
  setvbuf(fp, nullptr, _IOFBF, LOG_FILE_SYNC_THRESHOLD_BYTES);
  return fp;
}

void VmlogStore::rename_file_safe(const char* old_path, const char* new_path) {
  if (is_file(old_path)) {
    FXL_LOG(INFO) << "rotate vmlog file: "
                  << fxl::StringPrintf("%s to %s", old_path, new_path);
    if (rename(old_path, new_path) != 0) {
      FXL_LOG(WARNING) << "rename vmlog file failed: "
                       << fxl::StringPrintf("%s to %s, %s", old_path, new_path,
                                            ErrnoString(errno).c_str());
      return;
    }
    sync();
  }
}

void VmlogStore::remove_file_safe(const char* path) {
  if (is_file(path)) {
    FXL_LOG(INFO) << "remove vmlog file: " << path;
    if (remove(path) != 0) {
      FXL_LOG(WARNING) << "remove vmlog file failed: "
                       << fxl::StringPrintf("%s, %s", path, ErrnoString(errno).c_str());
      return;
    }
    sync();
  }
}

void VmlogStore::WriteDlogLocked(const char* line_buffer, int len, uint32_t flags) {
  int n = snprintf(log_with_prefix_, sizeof(log_with_prefix_),
                   "[VM:%2d]%.*s", vmid_, len, line_buffer);
  if (n > (int)sizeof(log_with_prefix_)) {
    n = sizeof(log_with_prefix_);
  }

  if (n < 0) {
    FXL_LOG(WARNING) << "snprintf error: " << n;
    return;
  }

  zx_status_t status = dlog_.write(n, log_with_prefix_, flags);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "dlog write error: " << status;
  }
}

void VmlogStore::WriteFileLocked(const char* line_buffer, int len) {
  if (!log_fp_) {
    FXL_LOG(ERROR) << "unable to open file: " << log_file_ << ", " << ErrnoString(errno);
    return;
  }

  zx_time_t now = zx_clock_get(ZX_CLOCK_MONOTONIC);
  int n = snprintf(log_with_prefix_, sizeof(log_with_prefix_), "[%05d.%03d][VM:%2d]%.*s",
                   (int) (now / ZX_SEC(1)),
                   (int) ((now / ZX_MSEC(1)) % 1000ULL),
                   vmid_, len, line_buffer);
  if (n < 0) {
    FXL_LOG(WARNING) << "snprintf error: " << n;
    return;
  }

  if (n > (int)sizeof(log_with_prefix_)) {
    n = sizeof(log_with_prefix_);
  }

  size_t written = fwrite(log_with_prefix_, 1, n, log_fp_);
  if (written != static_cast<size_t>(n)) {
    FXL_LOG(WARNING) << "vmlog file write error: " << ErrnoString(errno);
  }

  dirty_bytes_counter_.fetch_add(written);
}

void VmlogStore::WriteLog(const char* log, size_t len) {
  std::lock_guard<std::mutex> guard(store_lock_);
  int n = (int)len;

  if (unlikely(shutdown_.load())) {
    FXL_LOG(WARNING) << "log store is shutdown, ignore log";
    return;
  }

  if (n > VMLOG_STORE_LINE_BUFFER_SIZE) {
    FXL_LOG(WARNING) << "log line too long, truncate to "
                     << VMLOG_STORE_LINE_BUFFER_SIZE;
    n = VMLOG_STORE_LINE_BUFFER_SIZE;
  }

  if (flags_ & (TO_SERIAL | TO_CONSOLE)) {
    // default do not save vmlog to nebula log file
    uint32_t dlog_flags = ZX_LOG_FLAG_NO_FILE;

    dlog_flags |= (flags_ & TO_SERIAL) ? 0 : ZX_LOG_FLAG_NO_SERIAL;
    dlog_flags |= (flags_ & TO_CONSOLE) ? 0 : ZX_LOG_FLAG_NO_CONSOLE;
    WriteDlogLocked(log, n, dlog_flags);
  }

  if (flags_ & TO_FILE) {
    WriteFileLocked(log, n);
    CheckSizeAndRotateLocked();
  }
}

void VmlogStore::CheckSizeAndRotateLocked() {
  if (!log_fp_)
    return;

  fflush(log_fp_);
  int64_t file_size = get_file_size(log_file_.c_str());
  if (file_size < 0 ||
      file_size <= static_cast<int64_t>(kLogFileMaximumAllowedSize)) {
    return;
  }

  FXL_LOG(INFO) << "current vmlog exceeds file size limit, "
                << fxl::StringPrintf("vmlog file:%s,size:%llu",
                                     log_file_.c_str(),
                                     static_cast<unsigned long long>(file_size));

  FILE* old_fp = log_fp_;
  log_fp_ = nullptr;
  close_file_safely(old_fp);

  char cur_file[kLogFilenameMaxLen] = {};
  char new_file[kLogFilenameMaxLen] = {};

  snprintf(cur_file, sizeof(cur_file)-1, "%s.older", log_file_.c_str());
  remove_file_safe(cur_file);

  snprintf(cur_file, sizeof(cur_file)-1, "%s", log_file_.c_str());
  snprintf(new_file, sizeof(new_file)-1, "%s.older", log_file_.c_str());
  rename_file_safe(cur_file, new_file);

  FILE* new_fp = open_log_file(log_file_.c_str());
  if (!new_fp) {
    FXL_LOG(ERROR) << "Failed to reopen vmlog file after rotation";
    return;
  }

  log_fp_ = new_fp;
  dirty_bytes_counter_.store(0);
}

void VmlogStore::LogFileSyncLoop(void) {
  while (!shutdown_.load()) {
    zx_nanosleep(zx_deadline_after(ZX_SEC(LOG_FILE_SYNC_PERIOD_SEC)));

    std::lock_guard<std::mutex> guard(store_lock_);
    if (log_fp_ && dirty_bytes_counter_.load() > 0) {
      dirty_bytes_counter_.store(0);
      sync_file(log_fp_);
    }
  }

  std::lock_guard<std::mutex> guard(store_lock_);
  if (log_fp_) {
    dirty_bytes_counter_.store(0);
    sync_file(log_fp_);
  }
  FXL_LOG(INFO) << "LogFileSyncLoop thread stopped";
}

zx_status_t VmlogStore::LogFileRotate(std::string base_filename) {
  int max_backups = kVmlogMaxFileBackups;
  char old_file[kLogFilenameMaxLen] = {};
  char new_file[kLogFilenameMaxLen] = {};

  snprintf(old_file, sizeof(old_file)-1, "%s.%d", base_filename.c_str(), max_backups);
  remove_file_safe(old_file);

  snprintf(old_file, sizeof(old_file)-1, "%s.older.%d", base_filename.c_str(), max_backups);
  remove_file_safe(old_file);

  // Shift existing backups (e.g., log.1 -> log.2)
  for (int i = max_backups - 1; i >= 1; --i) {
    snprintf(old_file, sizeof(old_file)-1, "%s.%d", base_filename.c_str(), i);
    snprintf(new_file, sizeof(new_file)-1, "%s.%d", base_filename.c_str(), i+1);
    rename_file_safe(old_file, new_file);

    snprintf(old_file, sizeof(old_file)-1, "%s.older.%d", base_filename.c_str(), i);
    snprintf(new_file, sizeof(new_file)-1, "%s.older.%d", base_filename.c_str(), i+1);
    rename_file_safe(old_file, new_file);
  }

  // Rename current file to .1
  snprintf(old_file, sizeof(old_file)-1, "%s", base_filename.c_str());
  snprintf(new_file, sizeof(new_file)-1, "%s.1", base_filename.c_str());
  rename_file_safe(old_file, new_file);

  snprintf(old_file, sizeof(old_file)-1, "%s.older", base_filename.c_str());
  snprintf(new_file, sizeof(new_file)-1, "%s.older.1", base_filename.c_str());
  rename_file_safe(old_file, new_file);

  sync();
  return ZX_OK;
}

zx_status_t VmlogStore::Initialize(int32_t vmid, uint32_t flags) {
  zx_status_t status = zx::log::create(&dlog_, 0);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to open debuglog for vmlog: status=" << status;
    return status;
  }

  // TODO: investigate rename log file issue under /data/vmlog sub directory
  // if (!files::IsDirectory(kVmlogStorageDir) &&
  //     !files::CreateDirectory(kVmlogStorageDir)) {
  //   FXL_LOG(ERROR) << "Failed to create directory: " << kVmlogStorageDir;
  //   return ZX_ERR_IO;
  // }

  if (vm_names.find(vmid) == vm_names.end()) {
    FXL_LOG(ERROR) << "VM name not found for vmid: "<< vmid;
    return ZX_ERR_INVALID_ARGS;
  }

  std::string log_file = fxl::StringPrintf("%s/vm_%s.log", kVmlogStorageDir, vm_names[vmid].c_str());
  status = LogFileRotate(log_file);
  if (status != ZX_OK) {
    return status;
  }

  FILE *fp = open_log_file(log_file.c_str());
  if (!fp) return ZX_ERR_IO;

  FXL_LOG(INFO) << "vmlog save to file path: " << log_file;

  log_fp_ = fp;
  dirty_bytes_counter_.store(0);
  log_file_ = log_file;
  vm_name_ = vm_names[vmid];
  vmid_ = vmid;
  flags_ = flags;
  shutdown_.store(false);

  file_sync_thrd_ = std::thread(&VmlogStore::LogFileSyncLoop, this);
  return ZX_OK;
}

void VmlogStore::Shutdown(void) {
  if (shutdown_.exchange(true)) {
    return;
  }

  if (file_sync_thrd_.joinable()) {
    file_sync_thrd_.join();
  }

  std::lock_guard<std::mutex> guard(store_lock_);
  if (log_fp_) {
    close_file_safely(log_fp_);
    log_fp_ = nullptr;
  }
}

VmlogStore::VmlogStore() : log_with_prefix_{}, flags_(0u), shutdown_(false) {
  log_fp_ = nullptr;
}

VmlogStore::~VmlogStore() {
  Shutdown();
}
