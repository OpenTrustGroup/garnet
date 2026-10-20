// SPDX-License-Identifier: BSD-3-Clause


#include "allocator.h"

#include <nebula/device/board.h>
#include <zircon/status.h>
#include <zircon/threads.h>

#include <ddk/binding.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddktl/device.h>
#include <lib/zx/vmar.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/string_printf.h>
#include <libfdt.h>
#include <vector>

Allocator::Allocator(zx_device_t* parent, void* fdt)
    : Device(parent), fdt_(fdt) {}

static std::vector<int> find_nodes_with_string(const void* dtb,
                                               const char* search_string,
                                               bool mulit_nodes) {
  int offset = 0;
  int depth = 0;
  const char* node_name = NULL;
  char ch = '0';
  std::vector<int> node_offset;

  if ((!search_string) || (!dtb) || (strlen(search_string) < 1)) {
    return node_offset;
  }

  // Start from the reserved-memory node
  offset = fdt_path_offset(dtb, "/reserved-memory");
  for (depth = 0; (offset >= 0) && (depth >= 0);
       offset = fdt_next_node(dtb, offset, &depth)) {
    if (depth != 1) {
      continue;
    }

    node_name = fdt_get_name(dtb, offset, NULL);
    if (node_name == NULL) {
      continue;
    }

    // Check if node name contains the search string
    if (strstr(node_name, search_string)) {
      ch = node_name[strlen(node_name) - strlen(search_string) - 2];
      if (ch >= '0' && ch <= '9') {
        // match pattern: mblock-[0-9]+-<search_string>
        node_offset.push_back(offset);
      } else if (ch == 'k') {
        // match pattern: mblock-<search_string>
        node_offset.push_back(offset);
      } else {
        continue;
      }

      if (!mulit_nodes) {
        break;
      }
    }
  }

  return node_offset;
}

static int get_reserve_memory(void* dtb, const char* suffix) {
  std::vector<int> node_offset;
  node_offset = find_nodes_with_string(dtb, suffix, false);
  if (node_offset.size() == 1) {
    return node_offset[0];
  }
  return -1;
}

zx_status_t Allocator::DdkIoctl(uint32_t op,
                                const void* in_buf,
                                size_t in_len,
                                void* out_buf,
                                size_t out_len,
                                size_t* actual) {
  switch (op) {
    case IOCTL_GET_GUEST_MEMORY_WITH_PATH:
    case IOCTL_GET_GUEST_MEMORY:
    case IOCTL_GET_RESERVED_MEMORY: {
      if (in_len < sizeof(mem_req_t) || out_len < sizeof(mem_resp_t)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
      }
      auto req = static_cast<const mem_req_t*>(in_buf);
      auto resp = static_cast<mem_resp_t*>(out_buf);
      zx_status_t status;

      if (op == IOCTL_GET_GUEST_MEMORY)
        status = GetGuestMemory(reinterpret_cast<const char*>(req->compatible),
                                resp);
      else if (op == IOCTL_GET_GUEST_MEMORY_WITH_PATH)
        status = GetGuestMemoryWithPath(
            reinterpret_cast<const char*>(req->path), resp);
      else if (op == IOCTL_GET_RESERVED_MEMORY)
        status = GetReservedMemory(
            reinterpret_cast<const char*>(req->search_string), resp);
      else
        status = ZX_ERR_INVALID_ARGS;

      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "GetGuestMemory failed: "
                       << zx_status_get_string(status);
        return status;
      }

      *actual = sizeof(mem_resp_t);
      return ZX_OK;
    }
    case IOCTL_GET_SYSTEM_MEMORY: {
      if (out_len < sizeof(mem_resp_t))
        return ZX_ERR_BUFFER_TOO_SMALL;
      auto resp = static_cast<mem_resp_t*>(out_buf);
      zx_status_t status = GetSystemMemory(resp);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "GetSystemMemory failed: "
                       << zx_status_get_string(status);
        return status;
      }

      *actual = sizeof(mem_resp_t);
      return ZX_OK;
    }
    case IOCTL_GET_GUEST_DTB_MEMORY: {
      if (out_len < sizeof(zx_handle_t)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
      }
      auto resp = static_cast<zx_handle_t*>(out_buf);

      zx_status_t status = GetGuestDtbMemory(resp);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "GetGuestDtbMemory failed: "
                          << zx_status_get_string(status);
        return status;
      }
      *actual = sizeof(zx_handle_t);
      return ZX_OK;
    }
    case IOCTL_QUERY_PROP_WITH_PATH: {
      if (in_len < sizeof(mem_req_t) || out_len < sizeof(uint64_t)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
      }
      auto req = static_cast<const mem_req_t*>(in_buf);
      auto prop = static_cast<uint64_t*>(out_buf);
      auto status =
          GetPropWithPath(reinterpret_cast<const char*>(req->path), prop);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "GetPropWithPath failed: "
                       << zx_status_get_string(status);
        return status;
      }
      *actual = sizeof(uint64_t);
      return ZX_OK;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

static int fdt_getprop_cells_u64(void* dtb,
                                 int offset,
                                 const char* prop_name,
                                 int num_cells,
                                 ...) {
  int len;
  auto prop = reinterpret_cast<const uint32_t*>(
      fdt_getprop(dtb, offset, prop_name, &len));
  if ((size_t)len != num_cells * sizeof(uint64_t))
    return -FDT_ERR_BADSTATE;

  va_list args;
  va_start(args, num_cells);
  for (int i = 0; i < num_cells; i++) {
    uint64_t hi = fdt32_to_cpu(prop[i * 2]);
    uint64_t low = fdt32_to_cpu(prop[i * 2 + 1]);
    *va_arg(args, uint64_t*) = low | hi << 32;
  }
  va_end(args);
  return 0;
}

zx_status_t Allocator::GetGuestMemory(const char* compatible,
                                      mem_resp_t* out_resp) {
  if (!compatible || !out_resp) {
    return ZX_ERR_INVALID_ARGS;
  }

  int off = fdt_node_offset_by_compatible(fdt_, -1, compatible);
  if (off < 0) {
    FXL_LOG(ERROR) << "Failed to find compatible node: " << compatible;
    return ZX_ERR_NOT_FOUND;
  }

  uint64_t addr, size;
  if (fdt_getprop_cells_u64(fdt_, off, "reg", 2, &addr, &size) != 0) {
    FXL_LOG(ERROR) << "Failed to get reg property";
    return ZX_ERR_NOT_FOUND;
  }

  out_resp->addr = addr;
  out_resp->size = size;
  return ZX_OK;
}

zx_status_t Allocator::GetGuestMemoryWithPath(const char* path,
                                              mem_resp_t* out_resp) {
  if (!path || !out_resp) {
    return ZX_ERR_INVALID_ARGS;
  }

  std::string pathstr(path);
  std::string dir = "";
  std::string file = pathstr;

  size_t pos = pathstr.find_last_of("/");
  if (pos != std::string::npos) {
    dir = pathstr.substr(0, pos);
    file = pathstr.substr(pos + 1);
  }

  int off = fdt_path_offset(fdt_, dir.c_str());
  if (off < 0) {
    FXL_LOG(ERROR) << "Failed to find " << dir;
    return ZX_ERR_NOT_FOUND;
  }

  uint64_t addr, size;
  if (fdt_getprop_cells_u64(fdt_, off, file.c_str(), 2, &addr, &size) != 0) {
    FXL_LOG(ERROR) << "Failed to get reg property";
    return ZX_ERR_NOT_FOUND;
  }

  out_resp->addr = addr;
  out_resp->size = size;
  return ZX_OK;
}


int copy_dtb_to_vmo(const void *fdt_, zx_handle_t* out_resp) {
  int total_size = fdt_totalsize(fdt_);
  zx_handle_t dtb_vmo = ZX_HANDLE_INVALID;
  if (zx_vmo_create(MAX_DTB_SIZE, 0, &dtb_vmo) != ZX_OK) {
    FXL_LOG(ERROR) <<("Failed to create dtb_vmo");
    return -1;
  }
  if (total_size > MAX_DTB_SIZE) {
    FXL_LOG(ERROR) << "ERROR : total_size > MAX_DTB_SIZE, buffer too smoll can not be write";
    return -1;
  } else {
    if (zx_vmo_write(dtb_vmo, (void*)(fdt_), 0, total_size) != ZX_OK) {
      FXL_LOG(ERROR) << "zx_vmo_write fail";
    }
  }

  *out_resp = dtb_vmo;
  return ZX_OK;
}

zx_status_t Allocator::GetGuestDtbMemory(zx_handle_t* out_resp) {
  if (!out_resp) {
    return ZX_ERR_INVALID_ARGS;
  }
  int ret = copy_dtb_to_vmo(fdt_, out_resp);
  if (ret < 0) {
    FXL_LOG(ERROR) << "copy_dtb_to_vmo fail " << ret;
  }
  return ZX_OK;
}

zx_status_t Allocator::GetPropWithPath(const char* path, uint64_t* out_prop) {
  if (!path || !out_prop) {
    return ZX_ERR_INVALID_ARGS;
  }

  std::string pathstr(path);
  std::string dir = "";
  std::string file = pathstr;

  size_t pos = pathstr.find_last_of("/");
  if (pos != std::string::npos) {
    dir = pathstr.substr(0, pos);
    file = pathstr.substr(pos + 1);
  }

  int off = fdt_path_offset(fdt_, dir.c_str());
  if (off < 0) {
    FXL_LOG(ERROR) << "Failed to find " << dir;
    return ZX_ERR_NOT_FOUND;
  }

  uint64_t prop;
  if (fdt_getprop_cells_u64(fdt_, off, file.c_str(), 1, &prop) != 0) {
    FXL_LOG(ERROR) << "Failed to get reg property";
    return ZX_ERR_NOT_FOUND;
  }

  *out_prop = prop;
  return ZX_OK;
}

zx_status_t Allocator::GetReservedMemory(const char* search_string,
                                         mem_resp_t* out_resp) {
  int off = get_reserve_memory(fdt_, search_string);
  if (off < 0)
    return ZX_ERR_NOT_FOUND;

  uint64_t addr, size;
  if (fdt_getprop_cells_u64(fdt_, off, "reg", 2, &addr, &size) != 0) {
    FXL_LOG(ERROR) << "Failed to get reg property";
    return ZX_ERR_NOT_FOUND;
  }

  out_resp->addr = addr;
  out_resp->size = size;
  return ZX_OK;
}

zx_status_t Allocator::GetSystemMemory(mem_resp_t* out_resp) {
  if (!out_resp) {
    return ZX_ERR_INVALID_ARGS;
  }

  int off = fdt_path_offset(fdt_, "/memory");
  if (off < 0) {
    FXL_LOG(ERROR) << "Failed to find \"/memory\" in device tree";
    return ZX_ERR_NOT_FOUND;
  }

  uint64_t addr, size;
  if (fdt_getprop_cells_u64(fdt_, off, "reg", 2, &addr, &size) != 0) {
    FXL_LOG(ERROR) << "Failed to get reg property";
    return ZX_ERR_NOT_FOUND;
  }

  out_resp->addr = addr;
  out_resp->size = size;
  return ZX_OK;
}

zx_status_t Allocator::Bind(void* ctx, zx_device_t* parent) {
  zx_status_t status;
  platform_bus_protocol_t pbus;

  if ((status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &pbus)) !=
      ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get protocol: "
                   << zx_status_get_string(status);
    return status;
  }

  void* fdt;
  pbus_get_fdt(&pbus, &fdt);
  auto allocator = std::make_unique<Allocator>(parent, fdt);
  if (!allocator) {
    return ZX_ERR_NO_MEMORY;
  }

  status = allocator->DdkAdd("guest_memory_allocator", DEVICE_ADD_NON_BINDABLE);
  if (status != ZX_OK) {
    return status;
  }

  __UNUSED auto* dummy = allocator.release();
  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = Allocator::Bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(allocator, driver_ops, "allocator", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_GRT_GUEST_MEMORY_ALLOCATOR),
ZIRCON_DRIVER_END(allocator)
