#include <uapi/err.h>

#include <fuz_common.h>
#include <zircon/nslist.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

void fuz_fill_arg_zx_handle(function_description *description,
                            uint32_t arg_inx) {
  zx_handle_t port_out;
  zx_port_create(0, &port_out);
  register_fuz_zx_handle(port_out);
  description->arg[arg_inx] = port_out;
}

void fuz_fill_arg_zx_handle_buf(function_description *description,
                                uint32_t arg_inx) {
  zx_handle_t port_out;
  zx_port_create(0, &port_out);
  register_fuz_zx_handle(port_out);
  description->arg[arg_inx] = (uint64_t)port_out;
  description->arglen[arg_inx] = sizeof(uint64_t);
}

void fuz_fill_arg_zx_channel_handle(function_description *description,
                                    uint32_t arg_inx) {
  zx_handle_t channel1;
  zx_handle_t channel2;
  zx_channel_create(0, &channel1, &channel2);
  register_fuz_zx_handle(channel1);
  register_fuz_zx_handle(channel2);
  description->arg[arg_inx] = channel1;
}

void fuz_fill_arg_zx_socket_handle(function_description *description,
                                   uint32_t arg_inx) {
  zx_handle_t channel1;
  zx_handle_t channel2;
  zx_socket_create(0, &channel1, &channel2);
  register_fuz_zx_handle(channel1);
  register_fuz_zx_handle(channel2);
  description->arg[arg_inx] = channel1;
}

void fuz_fill_arg_zx_thread_handle(function_description *description,
                                   uint32_t arg_inx) {
  zx_handle_t thread;
  zx_thread_create(zx_process_self(), "", 0, 0, &thread);
  register_fuz_zx_handle(thread);
  description->arg[arg_inx] = thread;
}

void fuz_fill_arg_zx_process_handle(function_description *description,
                                    uint32_t arg_inx) {
  zx_handle_t process;
  zx_handle_t vmar;
  static const char pname[] = "zx_syscall_test_process";
  zx_process_create(zx_job_default(), pname, sizeof(pname), 0u, &process,
                    &vmar);
  register_fuz_zx_handle(process);
  register_fuz_zx_handle(vmar);
  description->arg[arg_inx] = process;
}

void fuz_fill_arg_zx_job_handle(function_description *description,
                                uint32_t arg_inx) {
  zx_handle_t job_child;
  zx_job_create(zx_job_default(), 0u, &job_child);
  register_fuz_zx_handle(job_child);
  description->arg[arg_inx] = job_child;
}

void fuz_fill_arg_zx_event_handle(function_description *description,
                                  uint32_t arg_inx) {
  zx_handle_t event;
  zx_event_create(0, &event);
  register_fuz_zx_handle(event);
  description->arg[arg_inx] = event;
}

void fuz_fill_arg_zx_timer_handle(function_description *description,
                                  uint32_t arg_inx) {
  zx_handle_t timer;
  zx_timer_create(0, ZX_CLOCK_MONOTONIC, &timer);
  register_fuz_zx_handle(timer);
  description->arg[arg_inx] = timer;
}

void fuz_fill_arg_zx_vmo_handle(function_description *description,
                                uint32_t arg_inx) {
  zx_handle_t vmo;
  size_t size = description->argmax[arg_inx];
  zx_vmo_create(size, 0, &vmo);
  register_fuz_zx_handle(vmo);
  description->arg[arg_inx] = vmo;
}

void fuz_fill_arg_zx_vmr_handle(function_description *description,
                                uint32_t arg_inx) {
  zx_handle_t process;
  zx_handle_t vmar;
  static const char pname[] = "zx_syscall_vmr_test_process";
  zx_process_create(zx_job_default(), pname, sizeof(pname), 0u, &process,
                    &vmar);
  register_fuz_zx_handle(process);
  register_fuz_zx_handle(vmar);
  description->arg[arg_inx] = vmar;
}

void fuz_fill_arg_zx_fifo_handle(function_description *description,
                                 uint32_t arg_inx) {
  zx_handle_t fifo1;
  zx_handle_t fifo2;
  zx_fifo_create(8, 8, 0, &fifo1, &fifo2);
  register_fuz_zx_handle(fifo1);
  register_fuz_zx_handle(fifo2);
  description->arg[arg_inx] = fifo1;
}

void fuz_fill_arg_zx_log_handle(function_description *description,
                                uint32_t arg_inx) {
  zx_handle_t log_handle;
  zx_log_create(0, &log_handle);
  register_fuz_zx_handle(log_handle);
  description->arg[arg_inx] = log_handle;
}

void fuz_fill_arg_zx_debuglog_handle(function_description *description,
                                     uint32_t arg_inx) {
  zx_handle_t log_handle;
  zx_debuglog_create(0, 0, &log_handle);
  register_fuz_zx_handle(log_handle);
  description->arg[arg_inx] = log_handle;
}
