// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <vector>

#include "garnet/bin/guest/bootloader/bootloader.h"
#include "garnet/bin/guest/guest_config.h"
#include "garnet/bin/guest/guest_features.h"
#include "garnet/bin/guest/tee-smc.h"
#include "garnet/bin/guest/host_vsock_endpoint_impl.h"
#include "garnet/lib/machina/fdt_utils.h"
#include "garnet/lib/machina/interrupt_controller.h"
#include "garnet/lib/machina/ipc_message.h"
#include "garnet/lib/machina/ipc_service_impl.h"
#include "garnet/lib/machina/monitor_virtio.h"
#include "garnet/lib/machina/pci.h"
#include "garnet/lib/machina/mmio_bus.h"
#include "garnet/lib/machina/remoteproc.h"
#include "garnet/lib/machina/uart.h"
#include "garnet/lib/machina/utrace.h"
#include "garnet/lib/machina/vcpu.h"
#include "garnet/lib/machina/virtio_console.h"
#include "garnet/lib/machina/virtio_block.h"
#include "garnet/lib/machina/virtio_rpmb.h"
#include "garnet/lib/machina/block_dispatcher.h"
#include "garnet/lib/machina/virtio_vsock.h"
#include "garnet/lib/machina/cross_vm_lock.h"
#include "garnet/lib/machina/pv_block.h"
#include "garnet/public/lib/guest_allocator/cpp/guest_allocator.h"
#include "garnet/lib/machina/interrupt_service_impl.h"
#include "garnet/lib/machina/mem_controller_impl.h"

#include <zircon/device/grt-wdt.h>
#include "fuchsia/cpp/virtualization.h"
#include "garnet/bin/guest/top.h"
#include "garnet/lib/machina/arch/arm64/smmu_v3.h"
#include "garnet/lib/machina/virtio_cluster.h"
#include "garnet/lib/machina/virtio_spi.h"
#include "garnet/lib/machina/virtio_i2c.h"
#include "garnet/lib/machina/virtio_eint.h"
#include "garnet/lib/machina/virtio_rtc.h"
#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/environment_services.h"
#include "lib/svc/cpp/service_provider_bridge.h"
#include "lib/zx/socket.h"
#include "garnet/lib/machina/gpu_ping.h"
#include "garnet/lib/machina/reg_access.h"
#include "garnet/lib/machina/watchdog.h"
#include "tipc_vqueue_notifier.h"
#include "vmlog_srv.h"
#include "gzfs_vsock_srv.h"
#include <fbl/function.h>

using virtualization::GuestController;
using virtualization::HostVsockEndpoint;

class Vmm : public GuestController {
 public:
  Vmm(component::ApplicationContext* app_context);
  ~Vmm();

  zx_status_t InitializeMmioVirtioBlock();
  zx_status_t InitializePciVirtioBlock();
  zx_status_t InitializePvBlock(uint16_t ufs_lun, uint32_t index);
  zx_status_t InitializeVirtioRpmb();
  zx_status_t Initialize(virtualization::Config vmm_cfg,
                         GuestFeatures features);
  zx_status_t StartPrimaryVcpu(std::function<void(zx_status_t)> stop_callback);
  void CreateNestedEnvironment();
  void Shutdown(fbl::Closure callback);
  void AddBinding(fidl::InterfaceRequest<GuestController> request) {
    guest_bindings_.AddBinding(this, std::move(request));
  }
  void NotifyClientsShutdown(void) { guest_bindings_.CloseAll(); }

  // |virtualization::GuestController|
  void GetConsole(GetConsoleCallback callback) override;
  void GetHostVsockEndpoint(
      fidl::InterfaceRequest<HostVsockEndpoint> endpoint) override;
  void SetLogLevel(int32_t log_level) override;

  zx_status_t PatchDeviceTree(uint64_t dtb_base, uint64_t dtb_size);
  zx_status_t PatchMdDtb(zx_handle_t vmo_handle);

 private:
  component::ApplicationContext* application_context_;
  component::ApplicationEnvironmentPtr env_;
  component::ApplicationEnvironmentControllerPtr env_controller_;
  component::ServiceProviderBridge service_provider_bridge_;
  component::ApplicationLauncherPtr env_launcher_;

  machina::Guest guest_;
  std::vector<std::unique_ptr<machina::Smmuv3>> vsmmus_;

  std::unique_ptr<machina::VhmServiceImpl> vhm_svc_;
  std::unique_ptr<machina::GpuPingServiceImpl> gpu_ping_svc_;
  std::unique_ptr<machina::IpcServiceImpl> ipc_mbox_svc_;
  std::unique_ptr<machina::IpcMessage> ipc_msg_;
  std::unique_ptr<RprocClient> rproc_client_;
  std::unique_ptr<machina::CrossVMLockGMClient> cvm_lock_gm_client_;
  std::unique_ptr<machina::CrossVMLockSOSServiceImpl> cvm_lock_sos_svc_;
  std::unique_ptr<TipcVqueueNotifier> vqueue_notifier_;
  std::unique_ptr<Bootloader> bootloader_;
  std::vector<fbl::unique_ptr<machina::VirtioBlock>> pci_blocks_;
  std::vector<fbl::unique_ptr<machina::VirtioBlock>> mmio_blocks_;
  std::vector<fbl::unique_ptr<machina::PvBlockDevice>> pv_blocks_;
  std::unique_ptr<machina::VirtioRpmb> rpmb_;
  std::vector<std::unique_ptr<machina::RegAccess>> reg_handlers_;
  std::unique_ptr<machina::InterruptServiceImpl> interrupt_svc_;
  std::unique_ptr<machina::MemControllerImpl> mem_controller_;

  machina::InterruptController interrupt_controller_;
  machina::GicIts gic_its_;
  machina::PciBus bus_;
  machina::MmioBus mmio_bus_;
  machina::Uart uart_;
  std::unique_ptr<machina::VirtioConsole> console_;
  zx::socket console_socket_;
  std::unique_ptr<machina::VirtioVsock> vsock_;
  HostVsockEndpointImpl host_vsock_endpoint_;

  std::unique_ptr<VmlogSrv> vmlog_sink_;
  std::unique_ptr<VmlogStore> log_store_;
  std::unique_ptr<GzFsVsockService> gzfs_vsock_srv_;

  std::unique_ptr<SpiTransportClient> spi_client_;
  std::unique_ptr<machina::VirtioCluster> cluster_;
  std::unique_ptr<machina::VirtioSPI> spi_;
  std::unique_ptr<machina::VirtioI2C> i2c_;
  std::unique_ptr<machina::VirtioEINT> eint_;
  std::unique_ptr<machina::VirtioRTC> rtc_;

  uintptr_t guest_ip_;
  std::vector<uint64_t> extra_params_;
  GuestConfig cfg_;

  fidl::BindingSet<GuestController> guest_bindings_;
  machina::Watchdog wdt_;
  GuestFeatures features_;
  bool nested_environment_created_ = false;
  bool shutdown_started_ = false;
};
