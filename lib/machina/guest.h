// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_GUEST_H_
#define GARNET_LIB_MACHINA_GUEST_H_

#include <fbl/function.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/unique_ptr.h>
#include <lib/async-loop/cpp/loop.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/types.h>
#include <zircon/vmid.h>

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

#include "garnet/lib/machina/guest_config.h"
#include "garnet/lib/machina/ipc_channel.h"
#include "garnet/lib/machina/phys_mem.h"
#include "garnet/lib/machina/vcpu.h"
#include "garnet/lib/machina/vhm_service_impl.h"
#include "garnet/lib/machina/vm_id.h"
#include "garnet/lib/machina/watchdog.h"
#include "garnet/lib/machina/watchdog_policy.h"
#include "garnet/lib/machina/gpu_scheduler.h"

struct reg_sched_shm_req {
  uint64_t shm_base;
  uint64_t shm_size;
};

struct reg_sched_irq_req {
  zx_handle_t guest_handle;
  uint32_t sched_irq;
};

struct reg_cvmd_shm_req {
  uint64_t shm_base;
  uint64_t shm_size;
};

struct reg_cvmd_irq_req {
  zx_handle_t guest_handle;
  uint32_t dump_irq;
};

struct reg_uos_exp_irq_reg {
  zx_handle_t guest_handle;
  uint32_t uos_exp_irq;
};


struct set_sched_id_req {
  zx_handle_t guest_handle;
  uint8_t sched_id;
};

struct set_bindlk_pcpu_req {
  zx_handle_t guest_handle;
  int32_t bindlk_pcpu;
};

enum {
  SCHED_PARAM_TYPE_DRAM_INFO = 0,
  SCHED_PARAM_TYPE_VM_MEM_INFO = 1,
  SCHED_PARAM_TYPE_VM_PREEMPT_COUNT_OFF = 2,
  SCHED_PARAM_TYPE_VM_POLICY_OFF = 3,
  SCHED_PARAM_TYPE_COMPLETE,
  SCHED_PARAM_TYPE_PERIOD,
  SCHED_PARAM_TYPE_BUDGET,
  SCHED_PARAM_TYPE_PRIORITY,
  SCHED_PARAM_TYPE_TIMESLICE,
  SCHED_PARAM_TYPE_VCPU_PRIORITY,
  SCHED_PARAM_TYPE_POLICY,
  SCHED_PARAM_TYPE_EXCLUSIVE_VCPU,
  SCHED_PARAM_TYPE_ONLINE_VCPU,
};
enum {
  SCHED_VCPU_STATUS_RT = 0,
  SCHED_VCPU_STATUS_SPIN_LOCK,
  SCHED_VCPU_STATUS_IRQ,
};
struct dram_info {
  uint64_t start;
  uint64_t size;
};
struct vm_mem_info {
  uint64_t phys_offset;
  uint64_t page_offset;
  uint64_t page_end;
  uint64_t kimage_voffset;
};
struct field_info {
  uint64_t offset;
  uint64_t size;
};
struct budget_info {
  uint64_t vmid;
  uint64_t vcpuid;
  uint64_t vcpu_handle;
  uint64_t budget;
};
struct period_info {
  uint64_t group;
  uint64_t period;
};
struct priority_info {
  uint64_t vcpu_status;
  uint64_t priority;
};
struct timeslice_info {
  uint64_t vcpu_status;
  uint64_t timeslice;
};
struct vcpu_priority_info {
    zx_handle_t vcpu_handle;
    uint32_t priority;
};
struct policy_info {
    uint64_t group;
    uint64_t policy;
};
struct online_vcpu_info {
    uint64_t vmid;
    uint64_t vcpu_mask;
};
struct ex_vcpu_info {
    uint64_t vmid;
    uint64_t vcpu_mask;
};
struct sched_param_req {
  uint64_t type;
  union {
    struct dram_info dram_info;
    struct vm_mem_info mem_info;
    struct field_info field_info;
    struct budget_info budget_info;
    struct period_info period_info;
    struct priority_info priority_info;
    struct timeslice_info timeslice_info;
    struct vcpu_priority_info vcpu_priority_info;
    struct policy_info policy_info;
    struct ex_vcpu_info ex_vcpu_info;
    struct online_vcpu_info online_vcpu_info;
  };
};
struct dt_dram_info { /* RAM configuration */
  unsigned int start_hi;
  unsigned int start_lo;
  unsigned int size_hi;
  unsigned int size_lo;
};

namespace machina {

struct GuestDeviceAccessPolicy {
  bool spi_dma_control_enabled = false;
  bool i2c_dma_control_enabled = false;
};

struct forward_ipi_call_arg {
  zx_txid_t txid;
  uint16_t vector;
  uint32_t irq_status_offset;
  uint32_t irq_clear_offset;
  uint64_t pending_bitmask;
  uint64_t timestamp;
};

struct forward_ipi_call_rsp {
  zx_txid_t txid;
  zx_status_t status;
};

enum class TrapType {
  MMIO_SYNC = 0,
  MMIO_BELL = 1,
  PIO_SYNC = 2,
};

class IoHandler;
class IoMapping;

class Guest {
 public:
  class GpaRange {
   public:
    GpaRange(const GpaRange&) = delete;
    GpaRange& operator=(const GpaRange&) = delete;

   private:
    friend class Guest;

    GpaRange(uintptr_t base, size_t size)
        : base_(base), size_(size), vmar_(ZX_HANDLE_INVALID) {}

    uintptr_t base_;
    size_t size_;
    zx_handle_t vmar_;
  };

  // TODO(alexlegg): Consolidate this constant with other definitions in Garnet.
  static constexpr size_t kMaxVcpus = 16u;

  zx_vaddr_t va_sec_boot;
  zx_vaddr_t va_sec_b;
  void* sbt_verify_mem = NULL;

  using VcpuArray = std::array<fbl::unique_ptr<Vcpu>, kMaxVcpus>;

  using VcpuFactory = fbl::Function<
      zx_status_t(Guest* guest, uintptr_t entry, uint64_t id, Vcpu* vcpu)>;

  using DeviceTreePatcher =
      fbl::Function<void(uint64_t dtb_base, uint64_t dtb_size)>;

  ~Guest();

  zx_status_t Init(const std::vector<machina::VmemSpec>& vmem);
  void ShutdownAsyncLoops();
  zx_status_t SetCpuAffinity(uint32_t* bind_pcpus, int size);
  zx_status_t SetUserVmid(int32_t user_vmid);
  zx_status_t TeeVmCreate(int32_t user_vmid);
  zx_status_t TeeVmDestroy(int32_t user_vmid);
  zx_status_t SetSchedId(uint8_t sched_id);
  zx_status_t SetSmcIRQ(uint32_t smc_irq);
  zx_status_t SetCvmLockIRQ(uint32_t cvm_lock_irq);
  zx_status_t SetCvmLockVMO(zx_handle_t cvm_lock_vmo_handle);
  zx_status_t SetBindLkPcpu(int32_t bindlk_pcpu);
  zx_status_t SetWakeupIrqs(uint32_t* wakeup_irq, int size);
  zx_status_t SetDefaultValue(const std::vector<machina::DefaultRegSpec>& reg_infos, bool vmchange);

  void SetCpuNums(uint32_t cpu_nums) { cpu_nums_ = cpu_nums; }
  void SetVmid(int32_t vmid) { vmid_ = vmid; }
  // Install once before any VCPU is started; legacy UOS uses software WDT.
  zx_status_t ConfigureWatchdogPolicy(WatchdogPolicy policy);
  const WatchdogPolicy& watchdog_policy() const {
    static const WatchdogPolicy legacy_policy(true);
    return watchdog_policy_ ? *watchdog_policy_ : legacy_policy;
  }
  void SetDeviceAccessPolicy(GuestDeviceAccessPolicy policy) {
    device_access_policy_ = policy;
  }

  const PhysMem& phys_mem() const { return phys_mem_; }
  zx_handle_t handle() const { return guest_; }
  async_t* device_async() const { return device_loop_.async(); }

  async_t* trapbell_async() const { return trapbell_loop_.async(); }

  // Setup a trap to delegate accesses to an IO region to |handler|.
  zx_status_t CreateMapping(TrapType type,
                            uint64_t addr,
                            size_t size,
                            uint64_t offset,
                            IoHandler* handler);

  zx_status_t MapPhysicalMemory(uintptr_t gpaddr,
                                std::vector<uintptr_t>& page_list,
                                bool writeable);
  zx_status_t MapPhysicalMemory(uintptr_t gpaddr, zx::vmo& vmo, bool writeable);
  zx_status_t UnmapPhysicalMemory(uintptr_t gpaddr, size_t length);
  zx_status_t ReserveGpaRange(uintptr_t gpaddr,
                              size_t size,
                              GpaRange** out_range);
  zx_status_t MapPhysicalMemory(GpaRange* range,
                                uintptr_t gpaddr,
                                uintptr_t paddr,
                                size_t size,
                                bool writeable);
  zx_status_t UnmapPhysicalMemory(GpaRange* range,
                                  uintptr_t gpaddr,
                                  size_t size);

  // Setup a handler function to run when an additional VCPU is brought up. The
  // factory should call Start on the new VCPU to begin executing the guest on a
  // new thread.
  void RegisterVcpuFactory(VcpuFactory factory);

  // Initializes a VCPU by calling the VCPU factory. The first VCPU must have id
  // 0.
  zx_status_t StartVcpu(uintptr_t entry, uint64_t id);

  // Signals an interrupt to the VCPUs indicated by |mask|.
  zx_status_t SignalInterrupt(uint32_t mask, uint16_t vector);

  // Signals an interrupt to the VCPUs indicated by |mask|.
  zx_status_t SignalLpiInterrupt(uint32_t mask, uint16_t vector);

  // Waits for all VCPUs associated with the guest to finish executing.
  zx_status_t Join();

  const VcpuArray& vcpus() const { return vcpus_; }
  const std::vector<machina::VmemSpec>& vmems() const { return vmems_; }

  zx_status_t HandleNebulaHyperCall(zx_vcpu_state_t* state);
  void RegisterVhmRequestHandler(VhmRequestHandler* handler);

  zx_status_t AddVmemDeviceTreeNodes(const DeviceTreeSpec& dtb_spec,
                                     uintptr_t phys_base);

  void Dump(std::vector<std::string>& vcpu_states);

  zx_handle_t vmar() { return vmar_; }
  void RegisterIpcRequestHandler(IpcRequestHandler* handler) {
    ipc_req_handler_ = handler;
  }
  void RegisterDeviceTreePatcher(DeviceTreePatcher patcher) {
    device_tree_patcher_ = fbl::move(patcher);
  }
  void RegisterDeviceTreeMdPatcher(DeviceTreePatcher patcher) {
    device_tree_md_patcher_ = fbl::move(patcher);
  }

  machina::DeviceTreeSpec dtb_spec() { return dtb_spec_; }

  void CreateDtbSpec(machina::DeviceTreeSpec dtb_spec) {
    dtb_spec_ = fbl::move(dtb_spec);
  }

  zx_status_t SchedMemFromDtb(uint64_t guest_phys_base);
  zx_status_t CrossVmDumpMemFromDtb(uint64_t guest_phys_base, const std::vector<uint32_t>& crossvm_times);
  static zx_status_t GetGICInterruptCellSize(void* fdt, uint32_t& cells_size);

  void SetTraceMem(uintptr_t paddr, size_t size) {
    trace_mem_pa_ = paddr;
    trace_mem_sz_ = size;
  }

  void SetMonitorVirtioMem(uintptr_t paddr, size_t size) {
    monitor_virtio_mem_pa_ = paddr;
    monitor_virtio_mem_sz_ = size;
  }

  zx_vaddr_t monitor_virtio_pa() { return monitor_virtio_mem_pa_; }
  size_t monitor_virtio_size() { return monitor_virtio_mem_sz_; }

  zx_vaddr_t trace_pa() { return trace_mem_pa_; }
  size_t trace_size() { return trace_mem_sz_; }

  void SetSchedMem(uintptr_t paddr, size_t size) {
    sched_mem_pa_ = paddr;
    sched_mem_sz_ = size;
  }
  void SetSchedIRQ(uint32_t sched_irq) { sched_irq_ = sched_irq; }
  void SetDumpIRQ(uint32_t dump_irq) { dump_irq_ = dump_irq; }
  void SetMonitorIRQ(uint32_t monitor_irq) {
    monitor_irq_ = monitor_irq;
  }

  zx_vaddr_t cvmd_mem_pa() { return cvmd_mem_pa_; }
  size_t cvmd_mem_size() { return cvmd_mem_sz_; }

  void SetCvmdMem(uintptr_t paddr, size_t size) {
    cvmd_mem_pa_ = paddr;
    cvmd_mem_sz_ = size;
  }

  uint32_t GetDumpIRQ() { return dump_irq_; }
  void SetUosExpIRQ(uint32_t uos_exp_irq) { uos_exp_irq_ = uos_exp_irq; };

  zx_status_t RegSchedIRQ();
  zx_status_t RegCVMDIRQ();

  bool IsUosException() { return uos_exception_; }
  void SetUosException(bool exception) { uos_exception_ = exception; }
  zx::event* vm_dmp_event() { return &vm_dump_event_; }
  uintptr_t sched_phys_base() { return sched_mem_pa_; }
  uintptr_t sched_size() { return sched_mem_sz_; }
  zx_status_t InitDump();
  zx_status_t InitCvmdDump();
  zx_status_t InitCDump(uintptr_t guest_phys_base, bool is_sos);
  zx_status_t InitSched(uintptr_t guest_phys_base, bool is_sos);
  zx_status_t Init_Sec_Boot(uintptr_t guest_phys_base);
  zx_status_t Release_Init_Sec_Boot();
  uintptr_t GetNblTraceBuffer() { return trace_mem_va_; }
  machina::GpuSchedulerServiceImpl* GetGpuSchedService() { return gpu_sched_service_.get(); }
  void SetGpuSchedService(fbl::unique_ptr<machina::GpuSchedulerServiceImpl> gpu_sched) { gpu_sched_service_ = fbl::move(gpu_sched); }

  template <typename T>
  T* Gpa2va(uint64_t gpa) {
    return phys_mem_.as<T>(gpa - phys_mem_.phys_base());
  }

  bool GpaValid(uint64_t gpa, uint32_t size);
  void Stop(zx_status_t status);

  uint32_t cpu_nums() { return cpu_nums_; }
  int32_t vmid() { return vmid_; }

  static uint64_t thread_id;
  int create_wdt_fd();
  void set_wdt_fd(int fd) { wdt_fd_ = fd; }
  int get_wdt_fd() { return wdt_fd_; }
  bool can_enable_crossvmdump() { return benable_crossvmdump_; }

  void set_stop_callback(std::function<void(zx_status_t)> stop_callback);

  void RegisterWatchdog(Watchdog *wdt) { wdt_ = wdt; };
  Watchdog *get_watchdog() { return wdt_; };

  void SetInterruptController(InterruptController *controller) {
    interrupt_controller_ = controller;
  }
  InterruptController* GetInterruptController() {
    return interrupt_controller_;
  }

 private:
  bool ContainsGpaRange(const GpaRange* range,
                        uintptr_t gpaddr,
                        size_t size) const;

  zx_status_t HandleIrqRequest(zx_vcpu_state_t* state);
  zx_status_t HandleIpcRequest(zx_vcpu_state_t* state);
  zx_status_t HandleGuestCall(zx_vcpu_state_t* state);

  fbl::Mutex mutex_;

  zx_handle_t guest_ = ZX_HANDLE_INVALID;
  zx_handle_t vmar_ = ZX_HANDLE_INVALID;
  std::vector<std::unique_ptr<GpaRange>> gpa_ranges_;
  PhysMem phys_mem_;

  fbl::SinglyLinkedList<fbl::unique_ptr<IoMapping>> mappings_;

  VcpuFactory vcpu_factory_ =
      [](Guest* guest, uintptr_t entry, uint64_t id, Vcpu* vcpu) {
        return ZX_ERR_BAD_STATE;
      };
  VcpuArray vcpus_;

  fbl::unique_ptr<machina::GpuSchedulerServiceImpl> gpu_sched_service_ = nullptr;

  VhmRequestHandler* vhm_req_handler_ = nullptr;
  IpcRequestHandler* ipc_req_handler_ = nullptr;
  DeviceTreePatcher device_tree_patcher_;
  DeviceTreePatcher device_tree_md_patcher_;
  machina::DeviceTreeSpec dtb_spec_;

  uint32_t cpu_nums_;
  uint32_t vcpu_nums_{0};

  async::Loop device_loop_;
  async::Loop trapbell_loop_;
  std::vector<machina::VmemSpec> vmems_;
  std::vector<machina::DefaultRegSpec> reg_infos_;

  zx_vaddr_t trace_mem_pa_;
  zx_vaddr_t trace_mem_va_;
  size_t trace_mem_sz_;
  zx::vmo trace_vmo_;

  zx_vaddr_t monitor_virtio_mem_pa_;
  zx_vaddr_t monitor_virtio_mem_va_;
  size_t monitor_virtio_mem_sz_;
  zx::vmo monitor_virtio_vmo_;

  uintptr_t sched_mem_pa_;
  zx_vaddr_t sched_mem_va_;
  size_t sched_mem_sz_;
  zx::vmo sched_vmo_;
  bool has_sched_mem_;
  uint32_t sched_irq_{0};
  uintptr_t cvmd_mem_pa_;
  zx_vaddr_t cvmd_mem_va_;
  size_t cvmd_mem_sz_;
  zx::vmo cvmd_vmo_;
  uint32_t dump_irq_{0};
  uint32_t monitor_irq_{0};
  int32_t vmid_;
  uint32_t uos_exp_irq_;
  uint32_t sched_id_;
  int32_t bindlk_pcpu_{-1};
  int wdt_fd_ = -1;
  uint64_t wdt_kick_call_count_ = 0;
  uint64_t wdt_kick_missing_log_count_ = 0;
  std::unique_ptr<const WatchdogPolicy> watchdog_policy_;
  bool uos_exception_ = false;
  bool benable_crossvmdump_ = false;
  zx::event vm_dump_event_;
  std::function<void(zx_status_t)> stop_callback_;
  Watchdog *wdt_ = nullptr;
  GuestDeviceAccessPolicy device_access_policy_;
  InterruptController *interrupt_controller_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_GUEST_H_
