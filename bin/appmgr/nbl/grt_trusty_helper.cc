// SPDX-License-Identifier: BSD-3-Clause

#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <unordered_map>

#include <stdlib.h>

#include <trusty_syscalls.h>
#include <trusty_std.h>
#include <uapi/err.h>

#include <lib/fxl/logging.h>
#include <lib/fxl/strings/split_string.h>

#include "grt_trusty_helper.h"

#define DIV_ROUND_UP(n, d) (((n) + (d)-1) / (d))

#define ROUNDUP(a, b) (((a) + ((b)-1)) & ~((b)-1))

#define UUID_LENGTH 36

struct ion_heap_info {
  zx_handle_t ion_heap_vmo;
  uint64_t heap_base;
  uint64_t heap_size;
};

typedef struct trusty_mem_map {
  std::string uuid;
  uint64_t base;
  size_t size;
} trusty_mem_map;

static std::unordered_map<std::string, ion_heap_info> g_process_heap_vmo;
static std::unordered_map<std::string, trusty_mem_map> g_trusty_mem_map;

extern "C" long enable_irq(uint32_t process_handle);
void enable_trusty_irq(uint32_t process_handle) {
  enable_irq(process_handle);
}

static void trusty_mem_map_add(std::string uuid, uint32_t base, uint32_t size);
void trusty_add_mmio_check(void) {
  char *mmio_config = getenv("trusty.exclusive_mmio");

  if ((mmio_config == nullptr) ||
      (strlen(mmio_config) == 0)) {
    return;
  }

  std::vector<std::string> config_items =
    fxl::SplitStringCopy(static_cast<std::string>(mmio_config),
                         ",", fxl::kTrimWhitespace,
                         fxl::kSplitWantAll);
  for (std::string &config : config_items) {
    std::vector<std::string> config_item =
      fxl::SplitStringCopy(config,
                           ":", fxl::kTrimWhitespace,
                           fxl::kSplitWantAll);
    if(config_item.size() != 3) {
      FXL_LOG(ERROR) << "Mmio config wrong:[" << config << "]";
      continue;
    }
    std::string uuid = config_item[0];
    uint32_t base = std::stoi(config_item[1], 0, 0);
    uint32_t size = std::stoi(config_item[2], 0, 0);
    if(uuid.size() != UUID_LENGTH) {
      FXL_LOG(ERROR) << "Mmio uuid wrong:[" << config_item[0] << "]";
      continue;
    }
    if((base == 0) && config_item[1] != "0x0") {
      FXL_LOG(ERROR) << "Mmio base wrong:[" << config_item[1] << "]";
      continue;
    }
    if(size == 0) {
      FXL_LOG(ERROR) << "Mmio size wrong:[" << config_item[2] << "]";
      continue;
    }
    trusty_mem_map_add(uuid, base, size);
  }
}

void get_trusty_app_uuid(component::ApplicationPackage* package, uuid_t* uuid) {
  uint32_t inx;
  component::ApplicationUUIDPtr app_uuid = std::move(package->uuid);

  if (app_uuid) {
    uuid->time_low = app_uuid->time_low;
    uuid->time_mid = app_uuid->time_mid;
    uuid->time_hi_and_version = app_uuid->time_hi_and_version;
    for (inx = 0; inx < 8; inx++) {
      uuid->clock_seq_and_node[inx] = app_uuid->clock_seq_and_node[inx];
    }
  }
}

static bool addr_region_intersects(uint64_t base1, size_t len1,
                            uint64_t base2, size_t len2);
bool trusty_mem_map_check(uuid_t *uuid,
                          uint32_t base,
                          uint32_t size) {
  char uuid_str[UUID_LENGTH + 1];
  snprintf(uuid_str, UUID_LENGTH + 1, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           uuid->time_low, uuid->time_mid, uuid->time_hi_and_version,
           uuid->clock_seq_and_node[0], uuid->clock_seq_and_node[1],
           uuid->clock_seq_and_node[2], uuid->clock_seq_and_node[3],
           uuid->clock_seq_and_node[4], uuid->clock_seq_and_node[5],
           uuid->clock_seq_and_node[6], uuid->clock_seq_and_node[7]);
  std::string uuid_string(uuid_str);

  auto map_mem_check = [base, size, uuid_string] (auto pair) -> bool {
    auto &map = pair.second;
    bool intersect = addr_region_intersects(map.base,
                                            map.size,
                                            base,
                                            size);
    if(!intersect) {
      return true;
    }
    if(uuid_string == map.uuid) {
      return true;
    }
    return false;
  };

  auto iter = std::find_if_not(g_trusty_mem_map.begin(),
                               g_trusty_mem_map.end(),
                               map_mem_check);
  if(iter != g_trusty_mem_map.end()) {
    FXL_LOG(ERROR) << "the trusty mem map can't be used by [" << uuid_string << "]";
    return false;
  }
  return true;
}

bool is_trusty_app(const char* path) {
  return (strstr(path, "reeta:/") != NULL) ||
         (strstr(path, "secmgr:/") != NULL) ||
         (strstr(path, "heeta:/") != NULL);
}

bool is_loader(const char* path) {
  return strcmp(path, "file:///system/bin/nbl_loader_srv") == 0;
}

bool is_gnapp_loader(const char* path) {
  if (strcmp(path, "file:///system/bin/nbl_loader_provider") == 0)
    return true;
  if (strcmp(path, "file://nbl_gp_manager") == 0)
    return true;
  return false;
}

zx_handle_t create_ion_heap_vmo(std::string url,
                                const std::vector<::fidl::StringPtr>& ion_args) {
  uint64_t heap_base = strtoul(ion_args[0]->c_str(), NULL, 10);
  uint64_t heap_size = strtoul(ion_args[1]->c_str(), NULL, 10);

  heap_size = ROUNDUP(heap_size, PAGE_SIZE);
  auto itersect = [heap_base, heap_size] (auto pair) -> bool {
    auto &map = pair.second;
    return addr_region_intersects(map.heap_base,
                                  map.heap_size,
                                  heap_base,
                                  heap_size);
  };
  auto iter = std::find_if(g_process_heap_vmo.begin(),
                           g_process_heap_vmo.end(),
                           itersect);
  if (iter != g_process_heap_vmo.end()) {
    FXL_LOG(ERROR) << "heap address is overlapped:" << url;
    return ZX_HANDLE_INVALID;
  }

  zx_handle_t heap_vmo;
  zx_status_t status =
      zx_trusty_create_heap_vmo(true, heap_base, heap_size, &heap_vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create heap vmo:[" << status << "]";
    return ZX_HANDLE_INVALID;
  }

  zx_handle_t dup_vmo;
  ion_heap_info* heap_info;
  if (zx_handle_duplicate(heap_vmo, ZX_RIGHT_SAME_RIGHTS, &dup_vmo) < 0) {
    FXL_LOG(ERROR) << "Failed to dup heap vmo.";
    goto dup_failed;
  }

  heap_info = &g_process_heap_vmo[url];
  heap_info->heap_base = heap_base;
  heap_info->heap_size = heap_size;
  heap_info->ion_heap_vmo = heap_vmo;
  return dup_vmo;

dup_failed:
  zx_trusty_destroy_heap_vmo(heap_vmo);
  zx_handle_close(heap_vmo);
  return ZX_HANDLE_INVALID;
}

zx_handle_t create_heap_vmo(uint64_t heap_size) {
  if (heap_size & (PAGE_SIZE - 1)) {
    FXL_LOG(ERROR) << "Heap size is not page aligned :(<< " << heap_size
                   << ").";
    return ZX_HANDLE_INVALID;
  }
  if (heap_size < 4 * 1024 * 1024) {
    FXL_LOG(ERROR) << "Heap size is too small:(<< " << heap_size << ").";
    return ZX_HANDLE_INVALID;
  }

  zx_handle_t heap_vmo;
  zx_status_t status =
      zx_trusty_create_heap_vmo(false, 0, heap_size, &heap_vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create heap vmo:[" << status << "]";
    return ZX_HANDLE_INVALID;
  }
  return heap_vmo;
}

static void destroy_ion_heap_vmo(std::string url);
void trusty_app_exit(std::string url) {
  destroy_ion_heap_vmo(url);
}

static void trusty_mem_map_add(std::string uuid, uint32_t base, uint32_t size) {
  trusty_mem_map *map;
  auto search = g_trusty_mem_map.find(uuid);
  if(search == g_trusty_mem_map.end()) {
    map = &g_trusty_mem_map[uuid];
    map->uuid = uuid;
    map->base = base;
    map->size = size;
  }
}

static void destroy_ion_heap_vmo(std::string url) {
  zx_status_t status;

  auto search = g_process_heap_vmo.find(url);
  if (search != g_process_heap_vmo.end()) {
    ion_heap_info* heap_info = &search->second;
    status = zx_trusty_destroy_heap_vmo(heap_info->ion_heap_vmo);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to destroy heap vmo:(" << status << ")";
    }
    status = zx_handle_close(heap_info->ion_heap_vmo);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to close heap vmo:(" << status << ")";
    }
    g_process_heap_vmo.erase(url);
  }
}

static bool addr_region_intersects(uint64_t base1, size_t len1,
                                   uint64_t base2, size_t len2) {
  // Can't overlap a zero-length region.
  if (len1 == 0 || len2 == 0) {
    return false;
  }

  if (base1 <= base2) {
    // doesn't intersect, 1 is completely below 2
    if (base1 + len1 <= base2)
      return false;
  } else if (base1 >= base2 + len2) {
    // 1 is completely above 2
    return false;
  }

  return true;
}
