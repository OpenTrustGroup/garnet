// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <mutex>
#include <vector>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>

#include <fuchsia/cpp/virtualization.h>
#include "lib/fsl/socket/socket_drainer.h"

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/environment_services.h"

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fxl/synchronization/thread_annotations.h"

#include "gzfs_vsock_proto.h"

class GzFsResponse {
 public:
  GzFsResponse(int32_t vmid, async_t* async);
  ~GzFsResponse();

  void Start(zx_handle_t socket) { socket_ = socket; }

  void SendResponse(response_t* resp, const void* data);

 private:
  struct Response {
    response_t hdr_;
    std::vector<char> data_;
    bool hdr_sent_ = false;
    size_t data_bytes_written_ = 0;
  };

  async_wait_result_t OnSocketReady(async_t* async,
                                    zx_status_t status,
                                    const zx_packet_signal* signal);

  zx_handle_t socket_ = ZX_HANDLE_INVALID;
  int32_t vmid_;
  async_t* async_;
  async::Wait wait_;
  std::mutex response_lock_;
  std::vector<Response> responses_ FXL_GUARDED_BY(response_lock_);
};

class GzFsVsockService : public fsl::SocketDrainer::Client,
                         public virtualization::HostVsockAcceptor {
 public:
  GzFsVsockService(int32_t vmid);
  ~GzFsVsockService() override;

  // |virtualization::HostVsockAcceptor|
  void Accept(uint32_t src_cid,
              uint32_t src_port,
              uint32_t port,
              AcceptCallback callback) override;

  fidl::InterfaceRequest<virtualization::HostVsockEndpoint>
  VsockEndpointNewRequest() {
    return vsock_endpoint_.NewRequest();
  }
  void Listen();
  void Close();

  // |fsl::SocketDrainer::Client|
  void OnDataAvailable(const void* data, size_t num_bytes) override;
  void OnDataComplete() override;

  enum RequestState {
    REQUEST_STATE_HEADER,
    REQUEST_STATE_DATA,
  };

 private:
  void ResetRequest() {
    std::lock_guard<std::mutex> guard(request_lock_);
    ResetRequestLocked();
  }

  void ResetRequestLocked() FXL_EXCLUSIVE_LOCKS_REQUIRED(request_lock_) {
    req_buf_.clear();
  }

  void Start(zx::socket socket) {
    response_handler_.Start(socket.get());
    socket_drainer_.Start(std::move(socket));
    ResetRequest();
  }

  // FUSE request handlers
  void GetAttr(const request_t* req, const void* data);
  void ReadDir(const request_t* req, const void* data);
  void Read(const request_t* req, const void* data);
  void StatFs(const request_t* req, const void* data);

  void HandleRequest(const request_t* req, const void* data);

  async::Loop loop_;
  std::mutex request_lock_;
  int32_t vmid_;
  uint32_t host_port_;
  std::atomic<bool> started_;
  std::vector<char> req_buf_ FXL_GUARDED_BY(request_lock_);
  fsl::SocketDrainer socket_drainer_;
  GzFsResponse response_handler_;
  fidl::Binding<virtualization::HostVsockAcceptor> vsock_binding_;
  virtualization::HostVsockEndpointPtr vsock_endpoint_;
};
