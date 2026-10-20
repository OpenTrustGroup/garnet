// SPDX-License-Identifier: BSD-3-Clause

#include "gzfs_vsock_srv.h"
#include <zircon/vmid.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <unordered_map>

namespace {

constexpr char kLocalStorageDir[] = "/data";
constexpr uint32_t kGzfsVsockHostPort = 5022;
constexpr uint32_t kFallbackBlockSize = 4096;

std::unordered_map<opcode_t, std::string> kOpcodeNames = {
    {OP_GETATTR, "OP_GETATTR"}, {OP_READDIR, "OP_READDIR"},
    {OP_OPEN, "OP_OPEN"},       {OP_READ, "OP_READ"},
    {OP_STATFS, "OP_STATFS"},
};

};  // namespace

GzFsResponse::GzFsResponse(int32_t vmid, async_t* async)
  : vmid_(vmid), async_(async) {
  FXL_LOG(INFO) << "GzFsResponse::GzFsResponse"
                << fxl::StringPrintf(": vmid %d", zx_log_guest_vmid(vmid));
}

GzFsResponse::~GzFsResponse() {
  FXL_LOG(INFO) << "GzFsResponse::~GzFsResponse";
  if (wait_.is_pending()) {
    wait_.Cancel(async_);
  }
}

void GzFsResponse::SendResponse(response_t* resp_hdr, const void* data) {
  if (socket_ == ZX_HANDLE_INVALID) {
    FXL_LOG(ERROR) << "Socket not initialized";
    return;
  }

  {
    std::lock_guard<std::mutex> guard(response_lock_);
    Response resp;

    memcpy((void*)&resp.hdr_, (void*)resp_hdr, sizeof(response_t));
    resp.hdr_sent_ = false;
    resp.data_bytes_written_ = 0;
    if (data && resp_hdr->length > 0) {
      resp.data_.resize(resp_hdr->length);
      memcpy(resp.data_.data(), data, resp_hdr->length);
    }
    responses_.push_back(std::move(resp));
  }

  async_wait_result_t result = OnSocketReady(async_, ZX_OK, nullptr);
  if (result == ASYNC_WAIT_AGAIN) {
    wait_.set_object(socket_);
    wait_.set_trigger(ZX_SOCKET_WRITABLE | ZX_SOCKET_WRITE_DISABLED |
                      ZX_SOCKET_PEER_CLOSED);
    wait_.set_handler(fbl::BindMember(this, &GzFsResponse::OnSocketReady));
    wait_.Begin(async_);
  }
}

async_wait_result_t GzFsResponse::OnSocketReady(
    async_t* async,
    zx_status_t status,
    const zx_packet_signal* signal) {
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed while waiting on socket: " << status;
    return ASYNC_WAIT_FINISHED;
  }

  if (signal != nullptr) {
    if (signal->observed & ZX_SOCKET_PEER_CLOSED) {
      FXL_LOG(ERROR) << "Socket closed";
      return ASYNC_WAIT_FINISHED;
    }

    if (!(signal->observed & ZX_SOCKET_WRITABLE)) {
      FXL_LOG(ERROR) << "Socket is not writable";
      return ASYNC_WAIT_FINISHED;
    }
  }

  std::lock_guard<std::mutex> guard(response_lock_);

  if (responses_.empty()) {
    return ASYNC_WAIT_FINISHED;
  }

  Response& resp = responses_.front();

  /* write response header to socket */
  if (resp.hdr_sent_ == false) {
    size_t bytes_written;
    status = zx_socket_write(socket_, 0, &resp.hdr_, sizeof(resp.hdr_),
                             &bytes_written);
    if (status == ZX_ERR_SHOULD_WAIT) {
      return ASYNC_WAIT_AGAIN;
    }

    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to write response header to socket: " << status;
      return ASYNC_WAIT_FINISHED;
    }

    FXL_CHECK(bytes_written == sizeof(resp.hdr_));
    resp.hdr_sent_ = true;
  }

  /* write response data to socket */
  while (resp.data_bytes_written_ < resp.data_.size()) {
    size_t bytes_written;
    size_t to_write = resp.data_.size() - resp.data_bytes_written_;
    status = zx_socket_write(socket_, 0,
                             resp.data_.data() + resp.data_bytes_written_,
                             to_write, &bytes_written);
    if (status == ZX_ERR_SHOULD_WAIT) {
      return ASYNC_WAIT_AGAIN;
    }

    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to write response data to socket: " << status;
      return ASYNC_WAIT_FINISHED;
    }

    resp.data_bytes_written_ += bytes_written;
    if (bytes_written < to_write) {
      return ASYNC_WAIT_AGAIN;
    }
  }

  FXL_VLOG(1) << __func__ << ": vmid=" << zx_log_guest_vmid(vmid_)
              << ", opcode=" << kOpcodeNames[static_cast<opcode_t>(resp.hdr_.opcode)]
              << ", seq=" << resp.hdr_.seq << ", result=" << resp.hdr_.result
              << ", length=" << resp.hdr_.length;

  responses_.erase(responses_.begin());
  return ASYNC_WAIT_FINISHED;
}

GzFsVsockService::GzFsVsockService(int32_t vmid)
    : loop_(nullptr),
      vmid_(vmid),
      host_port_(kGzfsVsockHostPort),
      started_(false),
      socket_drainer_(this, loop_.async()),
      response_handler_(vmid, loop_.async()),
      vsock_binding_(static_cast<virtualization::HostVsockAcceptor*>(this)) {
  FXL_LOG(INFO) << "GzFsVsockService::GzFsVsockService Constructor "
                << fxl::StringPrintf("vmid: %d", zx_log_guest_vmid(vmid));
  FXL_CHECK(loop_.StartThread() == ZX_OK);
}

GzFsVsockService::~GzFsVsockService() {
  FXL_LOG(INFO) << "GzFsVsockService::~GzFsVsockService Destructor "
                << fxl::StringPrintf("vmid: %d", zx_log_guest_vmid(vmid_));
  Close();
  if (vsock_binding_.is_bound()) {
    vsock_binding_.Unbind();
  }
  if (vsock_endpoint_.is_bound()) {
    vsock_endpoint_.Unbind();
  }
  loop_.Quit();
  loop_.JoinThreads();
}

static inline std::string make_local_path(const char* req_path) {
  std::string path(kLocalStorageDir);

  path.append(req_path);
  return path;
}

void GzFsVsockService::GetAttr(const request_t* req, const void* data) {
  getattr_req_t* getattr_req = (getattr_req_t*)data;
  auto local_path = make_local_path(getattr_req->path);
  struct stat st = {};
  errno = 0;

  int ret = lstat(local_path.c_str(), &st);

  response_t resp = {
      .opcode = OP_GETATTR,
      .seq = req->seq,
      .result = (ret == 0) ? 0 : -errno,
      .length = 0,
  };

  if (ret != 0) {
    if (errno != ENOENT) {
      FXL_LOG(ERROR) << "lstat failed for path:"
                     << fxl::StringPrintf(" %s, errno: %s", local_path.c_str(), strerror(errno));
    }
    response_handler_.SendResponse(&resp, nullptr);
    return;
  }

  getattr_resp_t resp_data = {.st = st};
  resp.length = sizeof(getattr_resp_t);
  response_handler_.SendResponse(&resp, &resp_data);
}

void GzFsVsockService::ReadDir(const request_t* req, const void* data) {
  readdir_req_t* readdir_req = (readdir_req_t*)data;
  auto local_path = make_local_path(readdir_req->path);

  DIR* dir = opendir(local_path.c_str());
  if (dir == NULL) {
    response_t resp = {
        .opcode = OP_READDIR,
        .seq = req->seq,
        .result = -errno,
        .length = 0,
    };
    FXL_LOG(ERROR) << "opendir failed for path: " << local_path << ", "
                   << strerror(errno);
    response_handler_.SendResponse(&resp, nullptr);
    return;
  }

  std::vector<dirent_t> entries;
  entries.reserve(10);

  struct dirent* de;
  while ((de = readdir(dir)) != NULL) {
    dirent_t entry = {
        .ino = de->d_ino,
        .type = de->d_type,
    };
    memset(entry.name, 0, sizeof(entry.name));
    strncpy(entry.name, de->d_name, sizeof(entry.name) - 1);
    entries.push_back(entry);
  }

  closedir(dir);

  response_t resp = {
      .opcode = OP_READDIR,
      .seq = req->seq,
      .result = 0,
      .length = (uint32_t)(entries.size() * sizeof(dirent_t)),
  };

  response_handler_.SendResponse(&resp, entries.data());
  return;
}

void GzFsVsockService::Read(const request_t* req, const void* data) {
  int saved_errno = 0;
  if (!req || !data) {
    FXL_LOG(ERROR) << "Invalid input parameters to Read";
    return;
  }

  read_req_t* read_req = (read_req_t*)data;

  auto local_path = make_local_path(read_req->path);

  int fd = open(local_path.c_str(), O_RDONLY);
  if (fd < 0) {
    saved_errno = errno;
    response_t resp = {
        .opcode = OP_READ,
        .seq = req->seq,
        .result = -saved_errno,
        .length = 0,
    };
    FXL_LOG(ERROR) << "open failed for path: " << local_path << ", "
                   << strerror(saved_errno);
    response_handler_.SendResponse(&resp, nullptr);
    return;
  }

  FXL_VLOG(1) << "pread: path=" << local_path << ", offset=" << read_req->offset
              << ", size=" << read_req->size;

  size_t total_buffer_size = sizeof(read_resp_t);
  if (read_req->size > SIZE_MAX - total_buffer_size) {
    FXL_LOG(ERROR) << "Buffer size calculation overflow for path: " << local_path;
    response_t resp = {
        .opcode = OP_READ,
        .seq = req->seq,
        .result = -ENOMEM,
        .length = 0,
    };
    close(fd);
    response_handler_.SendResponse(&resp, nullptr);
    return;
  }
  total_buffer_size += read_req->size;

  char* buffer = (char*)malloc(total_buffer_size);
  if (!buffer) {
    saved_errno = errno;
    FXL_LOG(ERROR) << "malloc failed for path: " << local_path << ", size: " << total_buffer_size << ", error: " << strerror(saved_errno);
    response_t resp = {
        .opcode = OP_READ,
        .seq = req->seq,
        .result = -ENOMEM,
        .length = 0,
    };
    close(fd);
    response_handler_.SendResponse(&resp, nullptr);
    return;
  }

  read_resp_t* resp_data = (read_resp_t*)buffer;

  ssize_t bytes_read = pread(fd, resp_data->data, read_req->size, read_req->offset);
  int pread_errno = (bytes_read < 0) ? errno : 0;

  close(fd);

  response_t resp = {
      .opcode = OP_READ,
      .seq = req->seq,
      .result = 0,
      .length = 0,
  };

  if (bytes_read >= 0) {
    resp_data->size = bytes_read;
    resp.result = 0;
    resp.length = sizeof(read_resp_t) + bytes_read;
  } else {
    resp.result = -pread_errno;
    FXL_LOG(ERROR) << "pread failed for path: " << local_path << ", error: " << strerror(pread_errno);
  }

  response_handler_.SendResponse(&resp, (bytes_read >= 0) ? buffer : nullptr);

  free(buffer);
}

void GzFsVsockService::StatFs(const request_t* req, const void* data) {
  statfs_t resp_data;
  auto local_path = make_local_path("/");
  int saved_errno = 0;

  memset(&resp_data, 0, sizeof(resp_data));

  struct statvfs stbuf = {};
  errno = 0;
  int ret = statvfs(local_path.c_str(), &stbuf);
  saved_errno = errno;
  if (ret == 0) {
    resp_data.blocks = stbuf.f_blocks;
    resp_data.bfree = stbuf.f_bfree;
    resp_data.bavail = stbuf.f_bavail;
    resp_data.files = stbuf.f_files;
    resp_data.ffree = stbuf.f_ffree;
    resp_data.bsize = stbuf.f_bsize ? stbuf.f_bsize : kFallbackBlockSize;
    resp_data.namelen = stbuf.f_namemax;
  } else {
    FXL_LOG(ERROR) << "gzfs statfs statvfs failed for path: " << local_path
                   << ", " << strerror(saved_errno);
  }

  response_t resp = {
      .opcode = OP_STATFS,
      .seq = req->seq,
      .result = (ret == 0) ? 0 : -saved_errno,
      .length = (ret == 0) ? (uint32_t)sizeof(statfs_t) : 0u,
  };

  response_handler_.SendResponse(&resp, (ret == 0) ? &resp_data : nullptr);
  return;
}

void GzFsVsockService::HandleRequest(const request_t* req, const void* data) {
  FXL_VLOG(1) << __func__ << ": vmid=" << zx_log_guest_vmid(vmid_)
              << ", opcode=" << kOpcodeNames[static_cast<opcode_t>(req->opcode)]
              << ", seq=" << req->seq << ", length=" << req->length;
  switch (req->opcode) {
    case OP_GETATTR:
      GetAttr(req, data);
      break;
    case OP_READDIR:
      ReadDir(req, data);
      break;
    case OP_READ:
      Read(req, data);
      break;
    case OP_STATFS:
      StatFs(req, data);
      break;
    default: {
      response_t resp = {
          .opcode = req->opcode,
          .seq = req->seq,
          .result = -EINVAL,
          .length = 0,
      };
      response_handler_.SendResponse(&resp, nullptr);
      break;
    }
  }
}

void GzFsVsockService::OnDataAvailable(const void* buf, size_t buf_size) {
  std::lock_guard<std::mutex> guard(request_lock_);

  FXL_VLOG(1) << __func__ << ": " << buf_size << " bytes received.";

  size_t current_size = req_buf_.size();
  req_buf_.resize(current_size + buf_size);
  memcpy(req_buf_.data() + current_size, buf, buf_size);

  do {
    // If we don't have a request header yet, wait for more data
    if (req_buf_.size() < sizeof(request_t)) {
      break;
    }

    // If we don't have the full request yet, wait for more data
    request_t* req_hdr = (request_t*)req_buf_.data();
    size_t full_req_size = sizeof(request_t) + req_hdr->length;
    if (req_buf_.size() < full_req_size) {
      break;
    }

    void* req_data_ptr = req_buf_.data() + sizeof(request_t);
    HandleRequest(req_hdr, req_data_ptr);
    req_buf_.erase(req_buf_.begin(), req_buf_.begin() + full_req_size);

    FXL_VLOG(1) << req_buf_.size() << " bytes remaining in buffer after processing request.";
  } while (!req_buf_.empty());
}

void GzFsVsockService::OnDataComplete(void) {
  FXL_LOG(INFO) << "Socket closed, change state to accept new connection.";
  started_.store(false);
  return;
};

// |virtualization::HostVsockAcceptor|
void GzFsVsockService::Accept(uint32_t src_cid,
                              uint32_t src_port,
                              uint32_t port,
                              AcceptCallback callback) {
  FXL_LOG(INFO) << "GzFsVsockService::Accept "
                << fxl::StringPrintf("vmid: %d", zx_log_guest_vmid(vmid_));
  if (port != host_port_) {
    FXL_LOG(ERROR) << "Unexpected port " << port;
    callback(ZX_ERR_CONNECTION_REFUSED, zx::handle());
    return;
  }

  if (started_.exchange(true)) {
    FXL_LOG(ERROR) << "Already have a connection, refuse new connection from guest cid: "
                   << src_cid << " port: " << src_port;
    callback(ZX_ERR_CONNECTION_REFUSED, zx::handle());
    return;
  }

  zx::socket socket, remote_socket;
  zx_status_t status =
      zx::socket::create(ZX_SOCKET_STREAM, &socket, &remote_socket);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create socket " << status;
    callback(ZX_ERR_CONNECTION_REFUSED, zx::handle());
    return;
  }

  Start(zx::socket(std::move(socket)));

  FXL_LOG(INFO) << "Accepted connection from guest cid: " << src_cid
                << " port: " << src_port;
  callback(ZX_OK, std::move(remote_socket));
}

void GzFsVsockService::Listen() {
  FXL_LOG(INFO) << "GzFsVsockService::Listen "
                << fxl::StringPrintf("vmid: %d", zx_log_guest_vmid(vmid_));
  if (!vsock_endpoint_.is_bound()) {
    FXL_LOG(ERROR) << "HostVsockEndpoint not bound, ignore listening on port "
                   << host_port_;
    return;
  }

  vsock_endpoint_->Listen(
      host_port_, vsock_binding_.NewBinding(),
      [vmid = vmid_, port = host_port_](zx_status_t status) {
        if (status != ZX_OK) {
          FXL_LOG(ERROR) << "Failed to listen on port " << port << ": "
                         << status;
        } else {
          FXL_LOG(INFO) << "Listening on port " << port
                        << ", vmid=" << zx_log_guest_vmid(vmid);
        }
      });
}

void GzFsVsockService::Close() {
  FXL_LOG(INFO) << "GzFsVsockService::Close "
                << fxl::StringPrintf("vmid: %d", zx_log_guest_vmid(vmid_));
  if (vsock_endpoint_.is_bound()) {
    FXL_LOG(INFO) << "HostVsockEndpoint is bound";
    vsock_endpoint_->Close(host_port_,
      [vmid = vmid_, port = host_port_](zx_status_t status) {
        FXL_LOG(INFO) << "GzFsVsockService::Close"
                << fxl::StringPrintf(" port=%d vmid=%d status=%d", port,
                                     zx_log_guest_vmid(vmid), status);
      });
  }
}
