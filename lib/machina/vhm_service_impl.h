// Copyright 2020 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <fuchsia/cpp/machina.h>
#include <lib/async-loop/cpp/loop.h>
#include <sys/types.h>
#include <zircon/syscalls/hypervisor.h>

#include <lib/fsl/vmo/sized_vmo.h>
#include "garnet/lib/machina/guest_config.h"
#include "garnet/lib/machina/interrupt_controller.h"
#include "garnet/lib/machina/phys_mem.h"
#include "garnet/lib/machina/rwlock.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"

struct acrn_io_request;
struct acrn_mmio_mapping;

struct mmio_mapping {
  std::unique_ptr<acrn_mmio_mapping> acrn_mmio_mapping;
  uint64_t trap_cnt;
};

namespace machina {

class VhmRequestHandler {
 public:
  virtual ~VhmRequestHandler() = default;
  virtual zx_status_t HandleVhmRequest(zx_vcpu_state_t* state) = 0;
};

class VmContext {
 public:
  static constexpr zx_signals_t kSignalIoreqCompletion = ZX_USER_SIGNAL_0;
  static constexpr zx_signals_t kSignalIoreqTermination = ZX_USER_SIGNAL_1;
  static constexpr zx_signals_t kSignalVmReady = ZX_USER_SIGNAL_2;

  VmContext(uint16_t vmid,
            uint8_t num_vcpus,
            uint64_t cpu_affinity,
            component::ApplicationContext* context,
            InterruptController* controller,
            uint32_t kick_irq);
  ~VmContext();

  void SetInterruptListener(fidl::InterfaceHandle<InterruptListener> handle);
  void SetMessageListener(fidl::InterfaceHandle<MessageListener> handle);
  void Wait(uint8_t vcpu_id);
  void GetMmioMappings(fidl::InterfaceHandle<MappingListener> handle);

  uint16_t vmid() { return vmid_; }
  uint8_t num_vcpus() { return num_vcpus_; }
  uint64_t cpu_affinity() { return cpu_affinity_; }
  void set_ioreq_buffer(uint64_t ioreq_buffer);
  void get_ioreq_vmo(zx::vmo* vmo);
  void signal_ioreq_completion(uint8_t vcpu_id);
  void signal_ioreq_termination(uint8_t vcpu_id);
  void send_interrupt(uint64_t msi_addr, uint64_t msi_data);
  zx_status_t vm_call(std::string& req, void *resp, size_t *resp_size);
  void start_vm();
  void add_mmio_mapping(std::unique_ptr<acrn_mmio_mapping>& mapping);
  bool is_message_listener_ready();

  void increase_trap_cnt(uintptr_t addr);
  void dump_trap_cnt();

  zx_status_t set_vm_config(const char* config_data, uint64_t config_size);
  void get_vm_config(fsl::SizedVmo* vmo);
  zx::event* get_vm_ready_event() { return &vm_ready_event_; };

#ifdef _VHM_USE_CHANNEL_
  void HandleVhmChannelMsgLoop();
  zx_status_t SetupVhmChannel(zx::channel &&srv, zx::channel &&cli);
#endif

 private:
  zx::vmo get_phys_vmo(uint64_t paddr, size_t size);

  uint16_t vmid_;
  uint8_t num_vcpus_;
  uint64_t cpu_affinity_;
  uint64_t ioreq_buffer_ = 0ul;
  acrn_io_request* ioreq_ = nullptr;
  std::vector<zx::event> ioreq_event_;
  zx::event vm_ready_event_;
  std::string vm_config_;

  std::vector<struct mmio_mapping> mmio_mappings_;

  async::Loop loop_;
  component::ApplicationContext* application_context_;
  component::ApplicationControllerSyncPtr application_controller_;

  InterruptListenerSyncPtr interrupt_listener_;
  MessageListenerSyncPtr message_listener_;

#ifdef _VHM_USE_CHANNEL_
  zx::channel vhm_srv_chan_;
  zx::channel vhm_cli_chan_;
#endif
  InterruptController* interrupt_controller_;
  uint32_t kick_irq_;

  FXL_DISALLOW_IMPLICIT_CONSTRUCTORS(VmContext);
};

class VhmServiceImpl : public VhmRequestHandler, public VhmService {
 public:
  VhmServiceImpl() = delete;
  VhmServiceImpl(component::ApplicationContext* application_context,
                 async_t* async,
                 InterruptController* interrupt_controller,
                 uint32_t kick_irq,
                 const PhysMem& phys_mem,
                 uintptr_t phys_mem_base,
                 Guest* guest);

 private:
  // |VhmService|
  virtual void GetIoRequestVmo(uint16_t vmid,
                               GetIoRequestVmoCallback callback) final;
  virtual void Kick() final;
  virtual void WaitForIoRequestCompletion(
      uint16_t vmid,
      uint8_t vcpu_id,
      WaitForIoRequestCompletionCallback callback) final;

  virtual void RegisterInterruptListener(
      uint16_t vmid,
      fidl::InterfaceHandle<InterruptListener> handle,
      RegisterInterruptListenerCallback callback);

  virtual void RegisterMessageListener(
      uint16_t vmid,
      fidl::InterfaceHandle<MessageListener> handle,
      RegisterMessageListenerCallback callback);

  virtual void FetchVmConfig(uint16_t vmid,
                             FetchVmConfigCallback callback) final;

  virtual void GetMmioMappings(uint16_t vmid,
                               fidl::InterfaceHandle<MappingListener> handle,
                               GetMmioMappingsCallback callback);

  virtual void SetupChannel(uint16_t vmid, SetupChannelCallback callback);

  // |VhmRequestHandler|
  virtual zx_status_t HandleVhmRequest(zx_vcpu_state_t* state) final;

  uint64_t mmio_trap_dump(uint64_t vmid);

  uintptr_t to_offset(uintptr_t paddr) { return paddr - phys_mem_base_; }

  uint64_t create_vm(uint64_t create_vm_paddr);
  uint64_t destroy_vm(uint64_t vmid);
  uint64_t start_vm(uint64_t vmid);
  uint64_t set_ioreq_buffer(uint64_t vmid, uint64_t ioreq_buffer);
  uint64_t signal_ioreq_completion(uint64_t vmid, uint8_t vcpu_id);
  uint64_t inject_msi(uint64_t vmid, uint64_t inject_msi_paddr);
  uint64_t set_vm_config(uint64_t vmid,
                         uint64_t config_addr,
                         uint64_t config_size);
  uint64_t add_mmio_mapping(uint64_t vmid, uint64_t mmio_mapping_paddr);
  uint64_t vm_call(uint64_t vmid,
                   uint64_t tx_buf_addr,
                   uint64_t tx_buf_size,
                   uint64_t rx_buf_addr,
                   uint64_t rx_buf_size);

  component::ApplicationContext* application_context_;
  fidl::BindingSet<VhmService> bindings_;

  std::mutex vm_map_lock_;
  std::unordered_map<uint64_t, std::shared_ptr<VmContext>> vm_map_;

  const PhysMem& phys_mem_;
  uintptr_t phys_mem_base_;
  InterruptController* interrupt_controller_;
  uint32_t kick_irq_;
  Guest *guest_;
};

}  // namespace machina
