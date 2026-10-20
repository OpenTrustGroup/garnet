// SPDX-License-Identifier: BSD-3-Clause

#include "garnet/bin/guest_manager/guest_manager.h"
#include "garnet/lib/machina/remoteproc_manager.h"
#include "garnet/lib/machina/cross_vm_lock.h"
#include "garnet/bin/smc_communication/smc_communication.h"
#include "garnet/bin/suspend/device_suspend.h"
#include "garnet/lib/features/features.h"
#include "garnet/public/lib/guest_allocator/cpp/guest_allocator.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"

#include <lib/app/cpp/application_context.h>
#include <lib/async-loop/cpp/loop.h>
#include <zircon/device/sysinfo.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <vector>

const std::string kDefaultVmmConfigPath = "/system/data/guest_manager.pb.txt";

static constexpr char kRprocConfigFilePath[] = "/system/data/rproc.pb.txt";
static constexpr char kResourcePath[] = "/dev/misc/sysinfo";
static constexpr char kGuestMemoryAllocator[] =
    "/dev/sys/platform/00:00:f/guest_memory_allocator";

static zx_status_t get_root_resource(zx::resource* resource) {
  int fd = open(kResourcePath, O_RDWR);
  if (fd < 0)
    return ZX_ERR_IO;
  zx_handle_t rsc_handle;
  ssize_t n = ioctl_sysinfo_get_root_resource(fd, &rsc_handle);
  resource->reset(rsc_handle);
  close(fd);
  return n < 0 ? ZX_ERR_IO : ZX_OK;
}

static bool file_exists(const char* filename) {
  struct stat statbuf;
  return stat(filename, &statbuf) == 0;
}

static zx_status_t create_rproc_manager(
    component::ApplicationContext* context,
    std::unique_ptr<machina::RprocManager>* out_mgr) {
  std::unique_ptr<machina::RprocManager> mgr;

  if (file_exists(kRprocConfigFilePath)) {
    mgr = machina::RprocManager::BuildFromFile(context, kRprocConfigFilePath);
  } else {
    FXL_LOG(INFO) << "Remoteproc config file not found, using default";
    mgr = machina::RprocManager::BuildWithDefaultConfig(context);
  }
  FXL_CHECK(mgr != nullptr);

  fxl::UniqueFD alloc_fd(open(kGuestMemoryAllocator, O_RDWR));
  if (!alloc_fd.is_valid()) {
    FXL_LOG(ERROR) << "Failed to open guest memory allocator";
    return ZX_ERR_INTERNAL;
  }

  mgr->AllocateSharedMemory([&alloc_fd](std::string& resv_mem) {
    mem_req req;
    mem_resp resp;
    strncpy(req.search_string, resv_mem.c_str(), sizeof(req.search_string));
    int ret = ioctl_get_reserved_memory(alloc_fd.get(), &req, &resp);

    zx::vmo shm_vmo;
    zx_status_t status;
    if (ret > 0) {
      zx::resource root_resource;
      status = get_root_resource(&root_resource);
      FXL_CHECK(status == ZX_OK);

      status = zx_vmo_create_physical(root_resource.get(), resp.addr, resp.size,
                                      shm_vmo.reset_and_get_address());
      FXL_CHECK(status == ZX_OK);

      FXL_LOG(INFO) << "Remoteproc reserved memory: " << resp.size / 0x100000
                    << " MiB";
    } else {
      FXL_LOG(INFO) << "Remoteproc reserved memory not found";
      status = zx::vmo::create(/*size=*/0x200000, /*option=*/0, &shm_vmo);
      FXL_CHECK(status == ZX_OK);
    }

    status = shm_vmo.set_cache_policy(ZX_CACHE_POLICY_CACHED);
    FXL_CHECK(status == ZX_OK);

    return shm_vmo;
  });

  *out_mgr = std::move(mgr);
  return ZX_OK;
}

int main(int argc, char** argv) {
  FXL_LOG(INFO) << "guest_manager start, "<< argv[0];
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto application_context =
      component::ApplicationContext::CreateFromStartupInfo();

  std::unique_ptr<machina::RprocManager> mgr;
  zx_status_t status = create_rproc_manager(application_context.get(), &mgr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create rproc manager, status=" << status;
    return EXIT_FAILURE;
  }

  machina::CrossVMLockGMServiceImpl cvm_lock_gm_svc(application_context.get());

  features::FeatureSet platform_features;
  status = features::FeatureSet::LoadFromSysinfo(&platform_features);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to load platform features: " << status;
    return EXIT_FAILURE;
  } else if (platform_features.smc_enabled()) {
    SmcCommunication* smc_communication = SmcCommunication::GetInstance();
    status = smc_communication->Initialize();
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to initialize SmcCommunication: " << status;
    }
  }

  DeviceSuspend* device_suspend = DeviceSuspend::GetInstance();
  status = device_suspend->Initialize();
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "Failed to initialize DeviceSuspend: " << status;
  }

  auto vmm_manager = GuestManagerImpl::BuildFromFile(
      application_context.get(), loop.async(), kDefaultVmmConfigPath,
      machina::WatchdogPolicy(platform_features.wdt_sink_enabled()));

  vmm_manager->LaunchGuests();
  loop.Run();
  return 0;
}
