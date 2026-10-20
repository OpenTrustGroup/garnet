// SPDX-License-Identifier: BSD-3-Clause

#include <map>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#include <cstdio>
#include <zircon/device/block.h>
#include <lib/zx/fifo.h>
#include <lib/zx/vmo.h>

#include "garnet/lib/block_locator/block_locator.h"
#include "parameter_parse.h"

namespace {

constexpr char kParameterPartitionName[] = "para";

}  // namespace

char* ParaParse::sysenv_get(const char*env) {
  if (sys_env_init_.load() == true) {
    std::lock_guard<std::mutex> lock(sys_env_mutex_);
    auto it = sys_env_map_.find(env);
    if (it != sys_env_map_.end()) {
      return strdup(it->second.c_str());
    }
  }
  return nullptr;
}

zx_status_t ParaParse::register_fast_block_io(const fbl::unique_fd& fd,
                                   zx_handle_t vmo,
                                   txnid_t* txnid_out,
                                   vmoid_t* vmoid_out,
                                   fifo_client_t** client_out) {
  zx::fifo fifo;
  if (ioctl_block_get_fifos(fd.get(), fifo.reset_and_get_address()) < 0) {
    FXL_LOG(ERROR) << "Couldn't attach fifo to partition";
    return ZX_ERR_IO;
  }
  if (ioctl_block_alloc_txn(fd.get(), txnid_out) < 0) {
    FXL_LOG(ERROR) << "Couldn't allocate transaction";
    return ZX_ERR_IO;
  }
  zx::vmo dup;
  if (zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS,
                          dup.reset_and_get_address()) != ZX_OK) {
    FXL_LOG(ERROR) << "Couldn't duplicate buffer vmo";
    return ZX_ERR_IO;
  }
  zx_handle_t h = dup.release();
  if (ioctl_block_attach_vmo(fd.get(), &h, vmoid_out) < 0) {
    FXL_LOG(ERROR) << "Couldn't attach VMO";
    return ZX_ERR_IO;
  }
  if (block_fifo_create_client(fifo.release(), client_out) != ZX_OK) {
    FXL_LOG(ERROR) << "Couldn't create block client";
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

zx_status_t ParaParse::DumpParaFromBlockDevice(const fbl::unique_fd& fd_in) {
  txnid_t txnid;
  vmoid_t vmoid;
  fifo_client_t* client;
  block_fifo_request_t request;
  zx_status_t status = ZX_OK;

  if (!fd_in) {
    FXL_LOG(ERROR) << "failed to open para partition";
    return ZX_ERR_IO;
  }

  block_info_t info;
  ssize_t len = ioctl_block_get_info(fd_in.get(), &info);
  if (len != sizeof(block_info_t)) {
    FXL_LOG(ERROR) << "failed to get block info";
    return ZX_ERR_IO;
  }
  FXL_LOG(INFO) << "Para block info:"
                << fxl::StringPrintf("block_count=0x%lx block_size=0x%x block_id=%d",
                                        info.block_count, info.block_size, info.block_id);

  size_t para_vmo_sz = info.block_count*info.block_size;
  if ((status = MappedVmo::Create(para_vmo_sz, "para-part", &Para_Vmo_)) != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create stream VMO,"
                   << fxl::StringPrintf("para_vmo_sz=0x%lx", para_vmo_sz);
    return ZX_ERR_NO_MEMORY;
  }

  status = register_fast_block_io(fd_in, Para_Vmo_->GetVmo(), &txnid, &vmoid, &client);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to register fast block io";
    return status;
  }

  request.txnid = txnid;
  request.vmoid = vmoid;
  request.opcode = BLOCKIO_READ;
  request.length = info.block_count;
  request.vmo_offset = 0;
  request.dev_offset = 0;

  status = block_fifo_txn(client, &request, 1);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to read payload";
    return status;
  }

  block_fifo_release_client(client);

  return status;
}

size_t ParaParse::ParseAndInsertEnvBlock(const uint8_t* block_start, const uint8_t* block_end) {
  if (block_end <= block_start) {
    return 0;
  }

  const char* data_start = reinterpret_cast<const char*>(block_start);
  const char* data_end = reinterpret_cast<const char*>(block_end);

  if (data_end - data_start < 8 || memcmp(data_start, "ENV_v1", 6) != 0) {
    FXL_LOG(ERROR) << "Invalid ENV_v1 block";
    return 0;
  }

  data_start += 8;

  size_t inserted_count = 0;
  const char* current = data_start;

  while (current < data_end && *current != '\0') {
    char line[2048] = {0};
    size_t line_len = 0;
    const char* line_end = current;
    while (line_end < data_end && *line_end != '\0') {
      line_len++;
      line_end++;
    }

    if (line_len == 0 || line_len >= sizeof(line)) {
      current++;
      continue;
    }

    memcpy(line, current, line_len);
    line[line_len] = '\0';

    char key[512] = {0};
    char value[512] = {0};

    int parsed = sscanf(line, "%511[^=]=%511s", key, value);

    std::lock_guard<std::mutex> lock(sys_env_mutex_);

    if (parsed == 2) {
      auto it = sys_env_map_.find(key);
      if (it != sys_env_map_.end()) {
        FXL_LOG(WARNING) << "Overwriting existing key: " << key
                         << " old value: " << it->second
                         << " new value: " << value;
      }
      sys_env_map_[key] = value;
      inserted_count++;
    } else if (parsed == 1) {
      sys_env_map_[key] = "";
      inserted_count++;
    }
    current = line_end + 1;
  }

  return inserted_count;
}

size_t ParaParse::ParsePara(MappedVmo* vmo) {
  size_t key_val_cnt;
  const size_t para_max_len = vmo->GetSize();
  uint8_t *para_data = reinterpret_cast<uint8_t*>(vmo->GetData());

  const char* target = "ENV_v1";
  const size_t target_len = strlen(target);
  std::vector<const uint8_t*> env_starts;
  std::vector<const uint8_t*> env_ends;

  for (size_t idx = 0; idx <= para_max_len - target_len; idx++) {
    if (memcmp(para_data + idx, target, target_len) == 0) {
        env_starts.push_back(para_data + idx);
    }
  }

  FXL_LOG(INFO) << "Found "
                << fxl::StringPrintf("%lu ENV_v1 signatures", env_starts.size());

  if (env_starts.empty() || (env_starts.size() != 2)) {
    FXL_LOG(ERROR) << "No ENV_v1 signature found";
    return ZX_ERR_NOT_FOUND;
  }

  const uint8_t* block_start = env_starts[0];
  const uint8_t* block_end = env_starts[1];

  FXL_LOG(INFO) << "ENV_v1 block "
                << fxl::StringPrintf(": start=0x%lx, end=0x%lx", (block_start - para_data), (block_end - para_data));

  const uint8_t* data_start = reinterpret_cast<const uint8_t*>(block_start);
  const uint8_t* data_end = reinterpret_cast<const uint8_t*>(block_end);
  key_val_cnt = ParseAndInsertEnvBlock(data_start, data_end);
  FXL_LOG(INFO) << "ENV_v1 block fount "
                << fxl::StringPrintf("%zu key-val", key_val_cnt);
  sys_env_init_.store(true);
  return key_val_cnt;
}

zx_status_t ParaParse::Initialize(void) {
  fbl::unique_fd para_fd;
  zx_status_t status = block_locator::OpenGptPartitionByName(
      kParameterPartitionName, block_locator::OpenMode::kReadOnly,
      ZX_TIME_INFINITE, &para_fd);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to resolve para partition by GPT name";
    return status;
  }

  status = DumpParaFromBlockDevice(para_fd);
  if (status != ZX_OK) {
    return status;
  }

  ParsePara(Para_Vmo_.get());
  Para_Vmo_.reset();
  return ZX_OK;
}

void ParaParse::DeInit(void) {
  FXL_LOG(INFO) << "DeInit";
  std::lock_guard<std::mutex> lock(sys_env_mutex_);
  sys_env_map_.clear();
}

ParaParse::ParaParse() {
  FXL_LOG(INFO) << "Constructor";
}

ParaParse::~ParaParse() {
  FXL_LOG(INFO) << "Destructor";
}
