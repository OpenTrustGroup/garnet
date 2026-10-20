// SPDX-License-Identifier: BSD-3-Clause

#include <algorithm>
#include <chrono>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/device/vfs.h>

#include "nebula_logger.h"

namespace {

#define LOG_FILE_MAX_BACKUPS 5
#define LOG_FILENAME_MAX_LEN 256
#define LOG_FILE_MAXIMUM_ALLOWED_SIZE (2 * 1024 * 1024)  // 2MB
#define LOG_FILE_SYNC_PERIOD_SEC 3
#define LOG_FILE_SYNC_THRESHOLD_BYTES (4096)             // 4KB sync threshold

static constexpr char default_log_file[] = "/data/nebula.log";
static constexpr char kDataDir[] = "/data";
static constexpr char kLittleFsName[] = "littlefs";
static constexpr int kLittleFsMountProbeCount = 10;
static constexpr zx_duration_t kLittleFsMountInitialDelay = ZX_MSEC(50);
static constexpr zx_duration_t kLittleFsMountProbeInterval = ZX_MSEC(100);

static std::string ErrnoString(int err) {
    return fxl::StringPrintf("%s(%d)", strerror(err), err);
}

static int usage(void) {
    fprintf(stderr,
        "usage: nebula_logger    dump the nebula debug log\n"
        "\n"
        "options: -f        don't exit, keep waiting for new messages\n"
        "         -p <pid>  only show messages from specified pid\n"
        "         -t        only show the text of messages (no metadata)\n"
        "         -s        saved nebula log to file\n"
        "         -h        show help\n"
    );
    return -1;
}

bool QueryFileSystemName(const char* path, std::string* out_name) {
  out_name->clear();
  errno = 0;
  int fd = open(path, O_RDONLY | O_DIRECTORY);
  int saved_errno = errno;
  if (fd < 0) {
    FXL_VLOG(1) << "vfs-query open-dir probe path=" << path
                << fxl::StringPrintf(" failed fd=%d errno=%d (%s)", fd,
                                     saved_errno, strerror(saved_errno));
    return false;
  }

  alignas(vfs_query_info_t) char query_buf[sizeof(vfs_query_info_t) +
                                             MAX_FS_NAME_LEN + 1] = {};
  auto* query = reinterpret_cast<vfs_query_info_t*>(query_buf);

  ssize_t ret = ioctl_vfs_query_fs(fd, query, sizeof(query_buf));
  saved_errno = errno;
  close(fd);

  if (ret < static_cast<ssize_t>(sizeof(vfs_query_info_t))) {
    FXL_VLOG(1) << "vfs-query probe path=" << path
                << fxl::StringPrintf(" failed ret=%zd errno=%d (%s)", ret,
                                     saved_errno, strerror(saved_errno));
    return false;
  }

  size_t name_len = static_cast<size_t>(ret) - sizeof(vfs_query_info_t);
  name_len = std::min(name_len, static_cast<size_t>(MAX_FS_NAME_LEN));
  const char* name = reinterpret_cast<const char*>(query) +
                     sizeof(vfs_query_info_t);
  out_name->assign(name, name_len);

  FXL_LOG(INFO) << "vfs-query probe path=" << path
                << fxl::StringPrintf(
                       " ok ret=%zd total_bytes=%llu used_bytes=%llu "
                       "total_nodes=%llu used_nodes=%llu fs_id=%llu "
                       "block_size=%u max_filename_size=%u fs_type=0x%x "
                       "name=%.*s",
                       ret,
                       static_cast<unsigned long long>(query->total_bytes),
                       static_cast<unsigned long long>(query->used_bytes),
                       static_cast<unsigned long long>(query->total_nodes),
                       static_cast<unsigned long long>(query->used_nodes),
                       static_cast<unsigned long long>(query->fs_id),
                       query->block_size, query->max_filename_size,
                       query->fs_type, static_cast<int>(name_len), name);
  return true;
}

bool WaitForMountedFileSystem(const char* path, const char* expected_name) {
  std::string fs_name;

  zx_nanosleep(zx_deadline_after(kLittleFsMountInitialDelay));
  for (int i = 0; i < kLittleFsMountProbeCount; ++i) {
    if (QueryFileSystemName(path, &fs_name) && fs_name == expected_name) {
      FXL_LOG(INFO) << "filesystem mounted: path=" << path
                    << ", name=" << fs_name;
      return true;
    }

    if (i + 1 < kLittleFsMountProbeCount) {
      zx_nanosleep(zx_deadline_after(kLittleFsMountProbeInterval));
    }
  }

  FXL_LOG(WARNING) << "filesystem is not mounted yet, continue logging: path="
                   << path << ", expected=" << expected_name
                   << ", last_name=" << fs_name;
  return false;
}

} // namespace

bool NebulaLogger::is_file(const char* path) {
    struct stat buf;
    if (stat(path, &buf) != 0)
        return false;
    return S_ISREG(buf.st_mode);
}

int64_t NebulaLogger::get_file_size(const char* path) {
    struct stat buf;
    if (stat(path, &buf) != 0)
        return -1;
    if (!S_ISREG(buf.st_mode))
        return -1;
    return static_cast<int64_t>(buf.st_size);
}

void NebulaLogger::sync_file(FILE* fp) {
    if (!fp) return;
    fflush(fp);
    fsync(fileno(fp));
}

void NebulaLogger::close_file_safely(FILE* fp) {
    if (!fp) return;
    sync_file(fp);
    fclose(fp);
}

FILE* NebulaLogger::open_log_file(const char* path) {
    FILE* fp = fopen(path, "w+");
    if (!fp) {
        FXL_LOG(ERROR) << "Could not open file: " << path << ", " << ErrnoString(errno);
        return nullptr;
    }
    setvbuf(fp, nullptr, _IOFBF, LOG_FILE_SYNC_THRESHOLD_BYTES);
    return fp;
}

void NebulaLogger::rename_file_safe(const char* old_path, const char* new_path) {
    if (is_file(old_path)) {
        FXL_LOG(INFO) << "rotate nebula log file: "
                      << fxl::StringPrintf("%s to %s", old_path, new_path);
        rename(old_path, new_path);
        sync();
    }
}

void NebulaLogger::remove_file_safe(const char* path) {
    if (is_file(path)) {
        FXL_LOG(INFO) << "remove nebula log file: " << path;
        remove(path);
        sync();
    }
}

int NebulaLogger::detect_fs_is_mounted(void) {
    return WaitForMountedFileSystem(kDataDir, kLittleFsName)
               ? FS_DETECT_SUCCESS
               : FS_DETECT_TIMEOUT;
}

void NebulaLogger::log_file_rotate(const char* base_filename) {
    int max_backups = LOG_FILE_MAX_BACKUPS;
    char old_file[LOG_FILENAME_MAX_LEN] = {};
    char new_file[LOG_FILENAME_MAX_LEN] = {};

    // Remove oldest backup files
    snprintf(old_file, sizeof(old_file)-1, "%s.%d", base_filename, max_backups);
    remove_file_safe(old_file);

    snprintf(old_file, sizeof(old_file)-1, "%s.older.%d", base_filename, max_backups);
    remove_file_safe(old_file);

    // Shift existing backup files
    for (int i = max_backups - 1; i >= 1; --i) {
        snprintf(old_file, sizeof(old_file)-1, "%s.%d", base_filename, i);
        snprintf(new_file, sizeof(new_file)-1, "%s.%d", base_filename, i+1);
        rename_file_safe(old_file, new_file);

        snprintf(old_file, sizeof(old_file)-1, "%s.older.%d", base_filename, i);
        snprintf(new_file, sizeof(new_file)-1, "%s.older.%d", base_filename, i+1);
        rename_file_safe(old_file, new_file);
    }

    // Rotate current log file
    snprintf(old_file, sizeof(old_file)-1, "%s", base_filename);
    snprintf(new_file, sizeof(new_file)-1, "%s.1", base_filename);
    rename_file_safe(old_file, new_file);

    snprintf(old_file, sizeof(old_file)-1, "%s.older", base_filename);
    snprintf(new_file, sizeof(new_file)-1, "%s.older.1", base_filename);
    rename_file_safe(old_file, new_file);

    sync();
}

size_t NebulaLogger::write_to_file(FILE* fp, zx_log_record_t* rec, bool plain) {
    if (!fp || !rec) return 0;

    size_t written_total = 0;

    if (!plain) {
        char tmp[32];
        int len = snprintf(tmp, sizeof(tmp), "(%lu)[%05d.%03d] ",
                           rec->cpu,
                           (int)(rec->timestamp / ZX_SEC(1)),
                           (int)((rec->timestamp / ZX_MSEC(1)) % 1000));
        if (len > 0) {
            written_total += fwrite(tmp, 1,
                                    std::min(static_cast<size_t>(len), sizeof(tmp)), fp);
        }
    }

    written_total += fwrite(rec->data, 1, rec->datalen, fp);
    if (rec->datalen == 0 || rec->data[rec->datalen - 1] != '\n') {
        if (fputc('\n', fp) != EOF) {
            ++written_total;
        }
    }
    return written_total;
}

void NebulaLogger::write_to_stdout(const zx_log_record_t& rec, bool plain) {
    write_to_file(stdout, const_cast<zx_log_record_t*>(&rec), plain);
}

NebulaLogger::QueuedLog::QueuedLog(const zx_log_record_t& rec) {
    timestamp = rec.timestamp;
    pid = rec.pid;
    tid = rec.tid;
    cpu = rec.cpu;
    flags = rec.flags;
    datalen = rec.datalen;
    memcpy(data, rec.data, datalen);
}

zx_status_t NebulaLogger::Initialize(bool plain) {
    plain_ = plain;
    worker_ = std::thread(&NebulaLogger::writer_thread, this);
    sync_worker_ = std::thread(&NebulaLogger::file_sync_thread, this);
    return ZX_OK;
}

NebulaLogger::NebulaLogger() {
    FXL_LOG(INFO) << "NebulaLogger Constructor";
}

NebulaLogger::~NebulaLogger() {
    stop();
}

void NebulaLogger::enqueue(const zx_log_record_t& rec) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        queue_.emplace(rec);
    }
    cv_.notify_one();
}

void NebulaLogger::flush_file(void) {
    std::lock_guard<std::mutex> lock(file_lock_);
    if (log_fp_) {
        sync_file(log_fp_);
        dirty_bytes_counter_.store(0);
    }
}

void NebulaLogger::stop(void) {
    if (exiting_.exchange(true))
        return;

    cv_.notify_all();
    sync_cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    if (sync_worker_.joinable()) {
        sync_worker_.join();
    }
}

void NebulaLogger::check_size_and_rotate() {
    std::lock_guard<std::mutex> lock(file_lock_);

    if (log_fp_ == nullptr)
        return;

    fflush(log_fp_);
    int64_t file_size = get_file_size(default_log_file);
    if (file_size < 0 ||
        file_size <= static_cast<int64_t>(LOG_FILE_MAXIMUM_ALLOWED_SIZE)) {
        return;
    }

    FXL_LOG(INFO) << "current nebula-logger exceeds file size limit, size: "
                  << file_size;

    // Close old file
    FILE* old_fp = log_fp_;
    log_fp_ = nullptr;
    close_file_safely(old_fp);

    char cur_file[LOG_FILENAME_MAX_LEN] = {};
    char new_file[LOG_FILENAME_MAX_LEN] = {};

    // Remove previous .older file
    snprintf(cur_file, sizeof(cur_file)-1, "%s.older", default_log_file);
    remove_file_safe(cur_file);

    // Rename current log to .older
    snprintf(cur_file, sizeof(cur_file)-1, "%s", default_log_file);
    snprintf(new_file, sizeof(new_file)-1, "%s.older", default_log_file);
    rename_file_safe(cur_file, new_file);

    // Open new clean log file
    FILE* new_fp = open_log_file(default_log_file);
    if (!new_fp) {
        FXL_LOG(ERROR) << "Failed to reopen log file after rotation";
        return;
    }

    log_fp_ = new_fp;
    dirty_bytes_counter_.store(0);
}

void NebulaLogger::file_sync_thread(void) {
    while (!exiting_.load()) {
        std::unique_lock<std::mutex> lock(sync_mu_);
        sync_cv_.wait_for(lock, std::chrono::seconds(LOG_FILE_SYNC_PERIOD_SEC), [this] {
            return exiting_.load();
        });

        if (exiting_.load())
            break;

        if (dirty_bytes_counter_.load() > 0) {
            flush_file();
        }
    }

    flush_file();
}

zx_status_t NebulaLogger::writer_thread(void) {
    // Detect filesystem status
    int ret = detect_fs_is_mounted();
    FXL_LOG(INFO) << "detect_fs_is_mounted ret: " << ret;

    // Rotate old logs and open new file
    FXL_LOG(INFO) << "nebula-logger: saving debug log to " << default_log_file;
    log_file_rotate(default_log_file);

    FILE* log_fp = open_log_file(default_log_file);
    if (!log_fp) return ZX_ERR_IO;

    {
        std::lock_guard<std::mutex> lock(file_lock_);
        log_fp_ = log_fp;
    }
    dirty_bytes_counter_.store(0);

    {
        std::lock_guard<std::mutex> lock(mu_);
        FXL_LOG(INFO) << "nebula-logger pending log records before writer loop: "
                      << queue_.size();
    }

    while (true) {
        QueuedLog entry;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this] {
                return !queue_.empty() || exiting_.load();
            });

            if (exiting_.load() && queue_.empty())
                break;

            entry = std::move(queue_.front());
            queue_.pop();
        }

        // Reconstruct log record
        zx_log_record_t rec{};
        rec.timestamp = entry.timestamp;
        rec.pid = entry.pid;
        rec.tid = entry.tid;
        rec.cpu = entry.cpu;
        rec.flags = entry.flags;
        rec.datalen = entry.datalen;
        memcpy(rec.data, entry.data, entry.datalen);

        // Write to file and check size
        size_t written = 0;
        {
            std::lock_guard<std::mutex> lock(file_lock_);
            written = write_to_file(log_fp_, &rec, plain_);
        }
        dirty_bytes_counter_.fetch_add(written);
        check_size_and_rotate();
    }

    // Cleanup on exit
    {
        std::lock_guard<std::mutex> lock(file_lock_);
        close_file_safely(log_fp_);
        log_fp_ = nullptr;
    }
    return ZX_OK;
}

int main(int argc, char** argv) {
    bool tail = false;
    bool filter_pid = false;
    bool plain = false;
    bool save_to_file = false;
    zx_koid_t pid = 0;
    zx_handle_t h;
    std::unique_ptr<NebulaLogger> logger_;

    FXL_LOG(INFO) << "nebula_logger start, " << argv[0];
    logger_ = std::make_unique<NebulaLogger>();
    if (!logger_) {
        FXL_LOG(ERROR) << "Failed to create NebulaLogger";
        return ZX_ERR_NO_MEMORY;
    }

    while (1) {
        static struct option opts[] = {
            {"tail", no_argument, nullptr, 'f'},
            {"plain", no_argument, nullptr, 't'},
            {"filter_pid", required_argument, nullptr, 'p'},
            {"save_to_file", no_argument, nullptr, 's'},
            {"help", no_argument, nullptr, 'h'},
            {nullptr, 0, nullptr, 0},
        };
        int opt_index;
        int c = getopt_long(argc, argv, "ftp:sh", opts, &opt_index);
        if (c < 0) break;

        switch (c) {
            case 'f': tail = true; break;
            case 't': plain = true; break;
            case 'p':
                errno = 0;
                pid = strtoull(optarg, NULL, 0);
                if (errno) {
                    fprintf(stderr, "nebula-logger: invalid pid\n");
                    return -1;
                }
                filter_pid = true;
                break;
            case 's': save_to_file = true; break;
            case 'h':
            default:
                return usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (save_to_file) {
        logger_->Initialize(plain);
    }

    if (zx_log_create(ZX_LOG_FLAG_READABLE, &h) < 0) {
        FXL_LOG(ERROR) << "nebula-logger: cannot open debug log";
        return -1;
    }

    char buf[ZX_LOG_RECORD_MAX] = {};
    zx_log_record_t* rec = (zx_log_record_t*)buf;
    for (;;) {
        zx_status_t status = zx_log_read(h, ZX_LOG_RECORD_MAX, rec, 0);
        if (status < 0) {
            if (status == ZX_ERR_SHOULD_WAIT) {
                logger_->flush_file();
                zx_object_wait_one(h, ZX_LOG_READABLE, ZX_TIME_INFINITE, NULL);
                continue;
            }
            break;
        }

        if (filter_pid && pid != rec->pid)
            continue;

        if (save_to_file) {
            if ((rec->flags & ZX_LOG_FLAG_NO_FILE) != 0)
                continue;
            logger_->enqueue(*rec);
        } else {
            logger_->write_to_stdout(*rec, plain);
        }
    }

    if (save_to_file) {
        logger_->stop();
    }

    return 0;
}
