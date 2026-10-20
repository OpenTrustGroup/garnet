// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <memory>

#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>
#include <pal/platform_defs.h>
#include <trusty_std.h>
#include <trusty_syscalls.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/logging_rate_limiter.h"
#include "lib/fxl/strings/string_printf.h"

class NebulaLogger {
public:
    NebulaLogger();
    ~NebulaLogger();

    NebulaLogger(const NebulaLogger&) = delete;
    NebulaLogger& operator=(const NebulaLogger&) = delete;

    zx_status_t Initialize(bool plain);
    void write_to_stdout(const zx_log_record_t& rec, bool plain);
    void enqueue(const zx_log_record_t& rec);
    void flush_file(void);
    void stop(void);

private:
    int detect_fs_is_mounted(void);
    bool is_file(const char* path);
    int64_t get_file_size(const char* path);
    void log_file_rotate(const char* base_filename);
    size_t write_to_file(FILE* fp, zx_log_record_t* rec, bool plain);
    zx_status_t writer_thread(void);
    void file_sync_thread(void);
    void check_size_and_rotate(void);
    void sync_file(FILE* fp);
    void close_file_safely(FILE* fp);
    FILE* open_log_file(const char* path);
    void rename_file_safe(const char* old_path, const char* new_path);
    void remove_file_safe(const char* path);

private:
    /**
     * @brief Struct to queue log records in memory
     */
    struct QueuedLog {
        zx_time_t timestamp;
        uint64_t pid;
        uint64_t tid;
        uint64_t cpu;
        uint32_t flags;
        uint32_t datalen;
        char data[ZX_LOG_RECORD_MAX];

        QueuedLog() = default;
        explicit QueuedLog(const zx_log_record_t& rec);
    };

private:
    FILE* log_fp_ = nullptr;
    std::atomic<uint64_t> dirty_bytes_counter_{0};
    std::mutex file_lock_;
    bool plain_;

    std::queue<QueuedLog> queue_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::mutex sync_mu_;
    std::condition_variable sync_cv_;

    std::atomic<bool> exiting_{false};
    std::thread worker_;
    std::thread sync_worker_;

    enum FileSystemStatus {
        FS_DETECT_FAILED        = -1,
        FS_DETECT_SUCCESS       = 0,
        FS_DETECT_TIMEOUT       = 1
    };
};
