/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/wait.h>
#include <zircon/types.h>
#include <chrono>
#include <condition_variable>
#include <mutex>

// Real definition — must fit into wait_queue_head_t::__opaque
struct wait_queue_head_impl {
  std::condition_variable cv;
  std::mutex mtx;
};

static_assert(sizeof(wait_queue_head_impl) <=
                  sizeof(((wait_queue_head_t*)0)->__opaque),
              "Opaque buffer too small");

static wait_queue_head_impl* impl(wait_queue_head_t* wq) {
  return reinterpret_cast<wait_queue_head_impl*>(wq);
}

void init_waitqueue_head(wait_queue_head_t* wq) {
  new (impl(wq)) wait_queue_head_impl;
}

void wake_up(wait_queue_head_t* wq) {
  impl(wq)->cv.notify_all();
}

long __wait_event_timeout_impl(wait_queue_head_t* wq, long timeout_ms) {
  auto p = impl(wq);
  std::unique_lock<std::mutex> lock(p->mtx);
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  while (true) {
    auto now = std::chrono::steady_clock::now();
    if (now >= deadline)
      return 0;  // timed out

    auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);

    if (p->cv.wait_for(lock, remaining) == std::cv_status::no_timeout) {
      // Woken up — loop back to let the macro check the condition
      return (remaining.count() > 0) ? remaining.count() : 1;
    }
  }
}