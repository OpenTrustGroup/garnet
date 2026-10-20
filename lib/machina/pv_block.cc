// SPDX-License-Identifier: BSD-3-Clause

#include "garnet/lib/machina/pv_block.h"

#include <errno.h>
#include <fcntl.h>
#include <new>
#include <unistd.h>

#include <utility>

#include <fbl/alloc_checker.h>
#include <fdio/io.h>
#include <trusty_std.h>
#include <zircon/device/block.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include "garnet/lib/machina/block_dispatcher.h"
#include "garnet/lib/machina/vm_id.h"
#include "lib/fxl/logging.h"

namespace machina {

namespace {

constexpr uint32_t kPvBlockDrainBudget = 0;
constexpr uint32_t kPvBlockIrqGraceUsec = 40;
constexpr uint32_t kPvBlockHotPathLogLimit = 8;
constexpr uintptr_t kPvBlockIrqPriority = 17;
constexpr uint32_t kPvBlockIrqCpuMask = 0xf0;

static uint64_t regs_u64(uint32_t lo, uint32_t hi) {
  return static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
}

static uint32_t low32(uint64_t value) {
  return static_cast<uint32_t>(value & 0xffffffffU);
}

static uint32_t high32(uint64_t value) {
  return static_cast<uint32_t>(value >> 32);
}

static uint32_t serial_word(const uint8_t* serial, size_t offset) {
  return static_cast<uint32_t>(serial[offset]) |
         (static_cast<uint32_t>(serial[offset + 1]) << 8) |
         (static_cast<uint32_t>(serial[offset + 2]) << 16) |
         (static_cast<uint32_t>(serial[offset + 3]) << 24);
}

static zx_status_t GuestGpaToHostVaddr(Guest* guest, uint64_t gpa,
                                       size_t len, void** out) {
  if (!guest || !out) {
    return ZX_ERR_INVALID_ARGS;
  }
  *out = nullptr;

  const auto& mem = guest->phys_mem();
  uint64_t phys_base = mem.phys_base();
  size_t mem_size = mem.size();
  uint64_t offset = gpa >= phys_base ? gpa - phys_base : gpa;
  if (offset > mem_size || len > mem_size - offset) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  *out = reinterpret_cast<void*>(mem.addr() + offset);
  return ZX_OK;
}

}  // namespace

zx_status_t PvBlockDevice::Create(const char* path, Guest* guest,
                                  InterruptController* interrupt_controller,
                                  uint64_t mmio_base, uint32_t irq,
                                  fbl::unique_ptr<PvBlockDevice>* out) {
  if (!path || !guest || !interrupt_controller || !out) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::unique_fd fd(open(path, O_RDWR));
  if (!fd) {
    FXL_LOG(ERROR) << "pvblk open failed path=" << path << " errno=" << errno;
    return ZX_ERR_IO;
  }

  return CreateFromFd(fd.release(), guest, interrupt_controller, mmio_base, irq,
                      out);
}

zx_status_t PvBlockDevice::CreateFromFd(
    int fd, Guest* guest, InterruptController* interrupt_controller,
    uint64_t mmio_base, uint32_t irq, fbl::unique_ptr<PvBlockDevice>* out) {
  fbl::unique_fd owned_fd(fd);
  if (!owned_fd || !guest || !interrupt_controller || !out) {
    return ZX_ERR_INVALID_ARGS;
  }

  int backend_vmid = GuestVmidToBlockBackendVmid(guest->vmid());
  if (!IsBlockBackendVmid(backend_vmid)) {
    FXL_LOG(ERROR) << "pvblk invalid backend vmid guest=" << guest->vmid()
                   << " backend=" << backend_vmid;
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker ac;
  fbl::unique_ptr<PvBlockDevice> dev(
      new (&ac) PvBlockDevice(std::move(owned_fd), guest, interrupt_controller,
                              static_cast<uint16_t>(backend_vmid), mmio_base,
                              irq));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = dev->Init();
  if (status != ZX_OK) {
    return status;
  }

  *out = fbl::move(dev);
  return ZX_OK;
}

PvBlockDevice::PvBlockDevice(fbl::unique_fd fd, Guest* guest,
                             InterruptController* interrupt_controller,
                             uint16_t backend_vmid, uint64_t mmio_base,
                             uint32_t irq)
    : fd_(std::move(fd)),
      guest_(guest),
      interrupt_controller_(interrupt_controller),
      backend_vmid_(backend_vmid),
      mmio_base_(mmio_base),
      irq_(irq) {}

PvBlockDevice::~PvBlockDevice() {
  Stop();
}

zx_status_t PvBlockDevice::Init() {
  zx_status_t status = StartBackend();
  if (status != ZX_OK) {
    return status;
  }

  status = AttachEvent();
  if (status != ZX_OK) {
    return status;
  }

  status = GetConfig();
  if (status != ZX_OK) {
    return status;
  }

  status = guest_->CreateMapping(TrapType::MMIO_SYNC, mmio_base_,
                                 PVBLK_MMIO_SIZE, 0, this);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "pvblk failed to map MMIO portal " << status;
    return status;
  }

  int ret = thrd_create_with_name(&irq_thread_, IrqThreadEntry, this,
                                  "pvblk-irq");
  if (ret != thrd_success) {
    FXL_LOG(ERROR) << "pvblk failed to create irq thread " << ret;
    irq_thread_ = 0;
    return ZX_ERR_INTERNAL;
  }

  FXL_LOG(INFO) << "pvblk ready vmid=" << backend_vmid_ << " mmio=0x"
                << std::hex << mmio_base_ << " irq=" << std::dec << irq_
                << " blocks=" << config_.block_count
                << " block_size=" << config_.block_size
                << " depth=" << config_.queue_depth
                << " features=0x" << std::hex << config_.feature_bits
                << std::dec
                << " ufs_lun=" << config_.ufs_lun
                << " wce=" << static_cast<uint32_t>(config_.wce)
                << " ro=" << static_cast<uint32_t>(config_.read_only)
                << " max_seg_size=" << config_.max_segment_size
                << " phys_block=" << config_.physical_block_size
                << " io_min=" << config_.io_min
                << " io_opt=" << config_.io_opt
                << " serial=" << reinterpret_cast<const char*>(config_.serial)
                << " discard_sectors=" << config_.discard_max_sectors
                << " discard_segs=" << config_.discard_max_segments
                << " discard_granularity=" << config_.discard_granularity
                << " write_zeroes_sectors="
                << config_.write_zeroes_max_sectors
                << " write_zeroes_segs="
                << config_.write_zeroes_max_segments
                << " secure_erase_sectors="
                << config_.secure_erase_max_sectors
                << " secure_erase_segs="
                << config_.secure_erase_max_segments
                << " secure_erase_granularity="
                << config_.secure_erase_granularity;
  return ZX_OK;
}

zx_status_t PvBlockDevice::Stop() {
  bool was_stopping = stopping_.exchange(true, std::memory_order_acq_rel);
  if (!was_stopping && completion_event_.is_valid()) {
    completion_event_.signal(0, ZX_USER_SIGNAL_0);
  }

  if (irq_thread_ != 0) {
    thrd_join(irq_thread_, nullptr);
    irq_thread_ = 0;
  }

  if (fd_) {
    uint16_t vmid = backend_vmid_;
    fdio_ioctl(fd_.get(), IOCTL_PVBLK_STOP, &vmid, sizeof(vmid), nullptr, 0);
  }
  completion_event_.reset();
  return ZX_OK;
}

zx_status_t PvBlockDevice::StartBackend() {
  struct pvblk_start_param start = {};
  start.vmid = backend_vmid_;
  start.queue_count = kPvBlockQueueCount;
  start.queue_depth = kPvBlockQueueDepth;
  start.mem.start = guest_->phys_mem().phys_base();
  start.mem.end = guest_->phys_mem().phys_base() + guest_->phys_mem().size() - 1;

  zx_status_t status =
      fdio_ioctl(fd_.get(), IOCTL_PVBLK_START, &start, sizeof(start), nullptr, 0);
  if (status < 0) {
    FXL_LOG(ERROR) << "pvblk start backend failed " << status;
    return status;
  }

  zx_handle_t vmo = ZX_HANDLE_INVALID;
  status = zx_handle_duplicate(guest_->phys_mem().vmo().get(),
                               ZX_RIGHT_SAME_RIGHTS, &vmo);
  if (status != ZX_OK) {
    return status;
  }

  status = fdio_ioctl(fd_.get(), IOCTL_PVBLK_SET_GPA_RANGE, &vmo, sizeof(vmo),
                      nullptr, 0);
  if (status < 0) {
    zx_handle_close(vmo);
    FXL_LOG(ERROR) << "pvblk set GPA VMO failed " << status;
    return status;
  }
  zx_handle_close(vmo);
  return ZX_OK;
}

zx_status_t PvBlockDevice::AttachEvent() {
  zx::event event;
  zx_status_t status = zx::event::create(0, &event);
  if (status != ZX_OK) {
    return status;
  }

  zx::event dup_event;
  status = event.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_event);
  if (status != ZX_OK) {
    return status;
  }

  zx_handle_t raw = dup_event.release();
  status = fdio_ioctl(fd_.get(), IOCTL_PVBLK_SET_EVENT, &raw, sizeof(raw),
                      nullptr, 0);
  if (status < 0) {
    zx_handle_close(raw);
    FXL_LOG(ERROR) << "pvblk set completion event failed " << status;
    return status;
  }
  zx_handle_close(raw);
  completion_event_ = std::move(event);
  return ZX_OK;
}

zx_status_t PvBlockDevice::GetConfig() {
  struct pvblk_config_param param = {
      .vmid = backend_vmid_,
  };
  struct pvblk_config config = {};
  zx_status_t status = fdio_ioctl(fd_.get(), IOCTL_PVBLK_GET_CONFIG, &param,
                                  sizeof(param), &config, sizeof(config));
  if (status < 0) {
    FXL_LOG(ERROR) << "pvblk get config failed " << status;
    return status;
  }
  if (config.magic != PVBLK_MAGIC ||
      config.version != PVBLK_ABI_VERSION ||
      !pvblk_queue_depth_valid(config.queue_depth)) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  config_ = config;
  return ZX_OK;
}

zx_status_t PvBlockDevice::AttachRingLocked() {
  if (ring_param_.qid >= config_.queue_count ||
      ring_param_.depth != config_.queue_depth) {
    return ZX_ERR_INVALID_ARGS;
  }

  ring_param_.vmid = backend_vmid_;
  zx_status_t status =
      fdio_ioctl(fd_.get(), IOCTL_PVBLK_SET_RING, &ring_param_,
                 sizeof(ring_param_), nullptr, 0);
  if (status < 0) {
    FXL_LOG(ERROR) << "pvblk set ring failed " << status;
    return status;
  }
  return ZX_OK;
}

zx_status_t PvBlockDevice::DoorbellLocked(uint16_t qid) {
  uint32_t sq_backlog = SqBacklogLocked();
  doorbell_param_.vmid = backend_vmid_;
  doorbell_param_.qid = qid;
  doorbell_param_.drain_budget = kPvBlockDrainBudget;
  zx_status_t status =
      fdio_ioctl(fd_.get(), IOCTL_PVBLK_DOORBELL, &doorbell_param_,
                 sizeof(doorbell_param_), nullptr, 0);
  uint32_t log_count = doorbell_log_count_.fetch_add(1, std::memory_order_relaxed);
  doorbell_count_.fetch_add(1, std::memory_order_relaxed);
  doorbell_backlog_total_.fetch_add(sq_backlog, std::memory_order_relaxed);
  if (sq_backlog == 0) {
    doorbell_empty_count_.fetch_add(1, std::memory_order_relaxed);
  }
  if (status < 0) {
    FXL_LOG(ERROR) << "pvblk doorbell failed vmid=" << backend_vmid_
                   << " qid=" << qid << " seq="
                   << doorbell_param_.doorbell_seq << " status=" << status;
    return status;
  }
  if (log_count < kPvBlockHotPathLogLimit) {
    FXL_VLOG(2) << "pvblk doorbell vmid=" << backend_vmid_ << " qid=" << qid
                << " seq=" << doorbell_param_.doorbell_seq
                << " sq_backlog=" << sq_backlog
                << " drain_budget=" << doorbell_param_.drain_budget
                << " count="
                << doorbell_count_.load(std::memory_order_relaxed);
  }
  return ZX_OK;
}

zx_status_t PvBlockDevice::ControlIoctlLocked() {
  void* arg = nullptr;
  control_result_ = 0;

  if (control_arg_len_ > PVBLK_CONTROL_ARG_MAX) {
    control_status_ = ZX_ERR_INVALID_ARGS;
    return ZX_OK;
  }

  zx_status_t status =
      GuestGpaToHostVaddr(guest_, control_arg_gpa_, control_arg_len_, &arg);
  if (status == ZX_OK) {
    status = DoVblockIoctl(fd_.get(), control_request_, arg, control_arg_len_,
                           "pvblk");
  }

  control_status_ = status;
  return ZX_OK;
}

zx_status_t PvBlockDevice::Read(uint64_t addr, IoValue* value) const {
  if (!value || value->access_size != 4) {
    return ZX_ERR_INVALID_ARGS;
  }
  uint32_t data = 0;
  zx_status_t status = ReadReg(addr, &data);
  if (status != ZX_OK) {
    return status;
  }
  value->u32 = data;
  return ZX_OK;
}

zx_status_t PvBlockDevice::Write(uint64_t addr, const IoValue& value) {
  if (value.access_size != 4) {
    return ZX_ERR_INVALID_ARGS;
  }
  return WriteReg(addr, value.u32);
}

zx_status_t PvBlockDevice::ReadReg(uint64_t addr, uint32_t* out) const {
  switch (addr) {
  case PVBLK_REG_MAGIC:
    *out = PVBLK_MMIO_MAGIC;
    return ZX_OK;
  case PVBLK_REG_VERSION:
    *out = PVBLK_MMIO_VERSION;
    return ZX_OK;
  case PVBLK_REG_BLOCK_SIZE:
    *out = config_.block_size;
    return ZX_OK;
  case PVBLK_REG_QUEUE_DEPTH:
    *out = config_.queue_depth;
    return ZX_OK;
  case PVBLK_REG_QUEUE_COUNT:
    *out = config_.queue_count;
    return ZX_OK;
  case PVBLK_REG_MAX_SEGS:
    *out = config_.max_segments;
    return ZX_OK;
  case PVBLK_REG_BLOCK_COUNT_LO:
    *out = low32(config_.block_count);
    return ZX_OK;
  case PVBLK_REG_BLOCK_COUNT_HI:
    *out = high32(config_.block_count);
    return ZX_OK;
  case PVBLK_REG_FEATURES_LO:
    *out = low32(config_.feature_bits);
    return ZX_OK;
  case PVBLK_REG_FEATURES_HI:
    *out = high32(config_.feature_bits);
    return ZX_OK;
  case PVBLK_REG_STATUS: {
    uint32_t status = PVBLK_STATUS_READY | PVBLK_STATUS_IRQ_READY;
    if (ring_param_.ctrl_gpa && ring_param_.sq_gpa && ring_param_.cq_gpa) {
      status |= PVBLK_STATUS_RING_READY;
    }
    *out = status;
    return ZX_OK;
  }
  case PVBLK_REG_SELECTED_Q:
    *out = ring_param_.qid;
    return ZX_OK;
  case PVBLK_REG_CTRL_GPA_LO:
    *out = low32(ring_param_.ctrl_gpa);
    return ZX_OK;
  case PVBLK_REG_CTRL_GPA_HI:
    *out = high32(ring_param_.ctrl_gpa);
    return ZX_OK;
  case PVBLK_REG_SQ_GPA_LO:
    *out = low32(ring_param_.sq_gpa);
    return ZX_OK;
  case PVBLK_REG_SQ_GPA_HI:
    *out = high32(ring_param_.sq_gpa);
    return ZX_OK;
  case PVBLK_REG_CQ_GPA_LO:
    *out = low32(ring_param_.cq_gpa);
    return ZX_OK;
  case PVBLK_REG_CQ_GPA_HI:
    *out = high32(ring_param_.cq_gpa);
    return ZX_OK;
  case PVBLK_REG_RING_DEPTH:
    *out = ring_param_.depth;
    return ZX_OK;
  case PVBLK_REG_DISCARD_MAX_SECTORS:
    *out = config_.discard_max_sectors;
    return ZX_OK;
  case PVBLK_REG_DISCARD_MAX_SEGS:
    *out = config_.discard_max_segments;
    return ZX_OK;
  case PVBLK_REG_DISCARD_GRANULARITY:
    *out = config_.discard_granularity;
    return ZX_OK;
  case PVBLK_REG_WRITE_ZEROES_MAX_SECTORS:
    *out = config_.write_zeroes_max_sectors;
    return ZX_OK;
  case PVBLK_REG_WRITE_ZEROES_MAX_SEGS:
    *out = config_.write_zeroes_max_segments;
    return ZX_OK;
  case PVBLK_REG_SECURE_ERASE_MAX_SECTORS:
    *out = config_.secure_erase_max_sectors;
    return ZX_OK;
  case PVBLK_REG_SECURE_ERASE_MAX_SEGS:
    *out = config_.secure_erase_max_segments;
    return ZX_OK;
  case PVBLK_REG_SECURE_ERASE_GRANULARITY:
    *out = config_.secure_erase_granularity;
    return ZX_OK;
  case PVBLK_REG_CONTROL_REQUEST:
    *out = control_request_;
    return ZX_OK;
  case PVBLK_REG_CONTROL_ARG_LO:
    *out = low32(control_arg_gpa_);
    return ZX_OK;
  case PVBLK_REG_CONTROL_ARG_HI:
    *out = high32(control_arg_gpa_);
    return ZX_OK;
  case PVBLK_REG_CONTROL_ARG_LEN:
    *out = control_arg_len_;
    return ZX_OK;
  case PVBLK_REG_CONTROL_RESULT:
    *out = control_result_;
    return ZX_OK;
  case PVBLK_REG_CONTROL_STATUS:
    *out = static_cast<uint32_t>(control_status_);
    return ZX_OK;
  case PVBLK_REG_UFS_LUN:
    *out = config_.ufs_lun;
    return ZX_OK;
  case PVBLK_REG_WRITEBACK:
    *out = config_.wce;
    return ZX_OK;
  case PVBLK_REG_SERIAL_0:
    *out = serial_word(config_.serial, 0);
    return ZX_OK;
  case PVBLK_REG_SERIAL_1:
    *out = serial_word(config_.serial, 4);
    return ZX_OK;
  case PVBLK_REG_SERIAL_2:
    *out = serial_word(config_.serial, 8);
    return ZX_OK;
  case PVBLK_REG_SERIAL_3:
    *out = serial_word(config_.serial, 12);
    return ZX_OK;
  case PVBLK_REG_SERIAL_4:
    *out = serial_word(config_.serial, 16);
    return ZX_OK;
  case PVBLK_REG_READ_ONLY:
    *out = config_.read_only;
    return ZX_OK;
  case PVBLK_REG_MAX_SEGMENT_SIZE:
    *out = config_.max_segment_size;
    return ZX_OK;
  case PVBLK_REG_PHYSICAL_BLOCK_SIZE:
    *out = config_.physical_block_size;
    return ZX_OK;
  case PVBLK_REG_ALIGNMENT_OFFSET:
    *out = config_.alignment_offset;
    return ZX_OK;
  case PVBLK_REG_IO_MIN:
    *out = config_.io_min;
    return ZX_OK;
  case PVBLK_REG_IO_OPT:
    *out = config_.io_opt;
    return ZX_OK;
  case PVBLK_REG_CRYPTO_MAX_DUN_BYTES:
    *out = config_.crypto_cap.max_dun_bytes_supported;
    return ZX_OK;
  case PVBLK_REG_CRYPTO_KEY_TYPES:
    *out = config_.crypto_cap.key_types_supported;
    return ZX_OK;
  case PVBLK_REG_CRYPTO_MODES_BASE + 0:
  case PVBLK_REG_CRYPTO_MODES_BASE + 4:
  case PVBLK_REG_CRYPTO_MODES_BASE + 8:
  case PVBLK_REG_CRYPTO_MODES_BASE + 12:
  case PVBLK_REG_CRYPTO_MODES_BASE + 16: {
    size_t index = (addr - PVBLK_REG_CRYPTO_MODES_BASE) / sizeof(uint32_t);
    *out = config_.crypto_cap.modes_supported[index];
    return ZX_OK;
  }
  default:
    return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t PvBlockDevice::WriteReg(uint64_t addr, uint32_t value) {
  if (addr == PVBLK_REG_COMMAND && value == PVBLK_CMD_STOP) {
    return Stop();
  }

  fbl::AutoLock lock(&mutex_);
  switch (addr) {
  case PVBLK_REG_SELECTED_Q:
    ring_param_.qid = static_cast<uint16_t>(value);
    return ZX_OK;
  case PVBLK_REG_CTRL_GPA_LO:
    ring_param_.ctrl_gpa =
        regs_u64(value, high32(ring_param_.ctrl_gpa));
    return ZX_OK;
  case PVBLK_REG_CTRL_GPA_HI:
    ring_param_.ctrl_gpa =
        regs_u64(low32(ring_param_.ctrl_gpa), value);
    return ZX_OK;
  case PVBLK_REG_SQ_GPA_LO:
    ring_param_.sq_gpa = regs_u64(value, high32(ring_param_.sq_gpa));
    return ZX_OK;
  case PVBLK_REG_SQ_GPA_HI:
    ring_param_.sq_gpa = regs_u64(low32(ring_param_.sq_gpa), value);
    return ZX_OK;
  case PVBLK_REG_CQ_GPA_LO:
    ring_param_.cq_gpa = regs_u64(value, high32(ring_param_.cq_gpa));
    return ZX_OK;
  case PVBLK_REG_CQ_GPA_HI:
    ring_param_.cq_gpa = regs_u64(low32(ring_param_.cq_gpa), value);
    return ZX_OK;
  case PVBLK_REG_RING_DEPTH:
    ring_param_.depth = value;
    return ZX_OK;
  case PVBLK_REG_COMMAND:
    if (value == PVBLK_CMD_SET_RING) {
      return AttachRingLocked();
    }
    if (value == PVBLK_CMD_CONTROL_IOCTL) {
      return ControlIoctlLocked();
    }
    return ZX_ERR_NOT_SUPPORTED;
  case PVBLK_REG_DOORBELL:
    return DoorbellLocked(static_cast<uint16_t>(value));
  case PVBLK_REG_DOORBELL_SEQ:
    doorbell_param_.doorbell_seq = value;
    return ZX_OK;
  case PVBLK_REG_WRITEBACK:
    if (value > 1U) {
      return ZX_ERR_INVALID_ARGS;
    }
    config_.wce = static_cast<uint8_t>(value);
    return ZX_OK;
  case PVBLK_REG_CONTROL_REQUEST:
    control_request_ = value;
    return ZX_OK;
  case PVBLK_REG_CONTROL_ARG_LO:
    control_arg_gpa_ = regs_u64(value, high32(control_arg_gpa_));
    return ZX_OK;
  case PVBLK_REG_CONTROL_ARG_HI:
    control_arg_gpa_ = regs_u64(low32(control_arg_gpa_), value);
    return ZX_OK;
  case PVBLK_REG_CONTROL_ARG_LEN:
    control_arg_len_ = value;
    return ZX_OK;
  case PVBLK_REG_CONTROL_RESULT:
    control_result_ = value;
    return ZX_OK;
  case PVBLK_REG_CONTROL_STATUS:
    control_status_ = static_cast<zx_status_t>(value);
    return ZX_OK;
  default:
    return ZX_ERR_NOT_SUPPORTED;
  }
}

const struct pvblk_queue_ctrl* PvBlockDevice::QueueCtrlLocked() const {
  if (!guest_ || ring_param_.depth == 0 || ring_param_.ctrl_gpa == 0) {
    return nullptr;
  }

  const auto& mem = guest_->phys_mem();
  uint64_t phys_base = mem.phys_base();
  size_t mem_size = mem.size();
  uint64_t gpa = ring_param_.ctrl_gpa;
  uint64_t offset = gpa >= phys_base ? gpa - phys_base : gpa;
  if (offset >= mem_size ||
      sizeof(struct pvblk_queue_ctrl) > mem_size - offset) {
    return nullptr;
  }
  return reinterpret_cast<const struct pvblk_queue_ctrl*>(mem.addr() + offset);
}

uint32_t PvBlockDevice::SqBacklogLocked() const {
  const struct pvblk_queue_ctrl* ctrl = QueueCtrlLocked();
  if (!ctrl) {
    return 0;
  }

  uint32_t sq_head = __atomic_load_n(&ctrl->sq_head, __ATOMIC_ACQUIRE);
  uint32_t sq_tail = __atomic_load_n(&ctrl->sq_tail, __ATOMIC_ACQUIRE);
  return sq_tail - sq_head;
}

bool PvBlockDevice::HasPendingCompletions() const {
  fbl::AutoLock lock(&mutex_);
  const struct pvblk_queue_ctrl* ctrl = QueueCtrlLocked();
  if (!ctrl) {
    return true;
  }

  uint32_t cq_head = __atomic_load_n(&ctrl->cq_head, __ATOMIC_ACQUIRE);
  uint32_t cq_tail = __atomic_load_n(&ctrl->cq_tail, __ATOMIC_ACQUIRE);
  return cq_head != cq_tail;
}

bool PvBlockDevice::WaitForGuestCompletionDrain() const {
  uint64_t deadline_ticks;
  uint64_t ticks_per_second;
  uint64_t grace_ticks;

  fbl::AutoLock lock(&mutex_);
  const struct pvblk_queue_ctrl* ctrl = QueueCtrlLocked();
  if (!ctrl) {
    return false;
  }

  if (__atomic_load_n(&ctrl->cq_head, __ATOMIC_ACQUIRE) ==
      __atomic_load_n(&ctrl->cq_tail, __ATOMIC_ACQUIRE)) {
    return true;
  }

  if (kPvBlockIrqGraceUsec == 0) {
    return false;
  }

  ticks_per_second = zx_ticks_per_second();
  grace_ticks = (ticks_per_second * kPvBlockIrqGraceUsec) / 1000000ULL;
  if (!grace_ticks) {
    grace_ticks = 1;
  }
  deadline_ticks = zx_ticks_get() + grace_ticks;

  do {
    __atomic_signal_fence(__ATOMIC_ACQUIRE);
    if (__atomic_load_n(&ctrl->cq_head, __ATOMIC_ACQUIRE) ==
        __atomic_load_n(&ctrl->cq_tail, __ATOMIC_ACQUIRE)) {
      return true;
    }
  } while (zx_ticks_get() < deadline_ticks);

  return __atomic_load_n(&ctrl->cq_head, __ATOMIC_ACQUIRE) ==
         __atomic_load_n(&ctrl->cq_tail, __ATOMIC_ACQUIRE);
}

void PvBlockDevice::NotifyGuest() {
  uint32_t log_count = notify_log_count_.fetch_add(1, std::memory_order_relaxed);
  notify_count_.fetch_add(1, std::memory_order_relaxed);
  if (log_count < kPvBlockHotPathLogLimit) {
    FXL_VLOG(2) << "pvblk notify guest vmid=" << backend_vmid_
                << " irq=" << irq_
                << " count=" << notify_count_.load(std::memory_order_relaxed);
  }
  interrupt_controller_->Interrupt(irq_);
}

int PvBlockDevice::IrqThreadEntry(void* arg) {
  return static_cast<PvBlockDevice*>(arg)->IrqThread();
}

int PvBlockDevice::IrqThread() {
  uint32_t cpu_mask = kPvBlockIrqCpuMask;
  _trusty_ioctl(SYS_PLATFORM_FD, SYS_PLATFORM_SET_CUR_THREAD_AFFINITY,
                &cpu_mask);
  zx_thread_set_priority(kPvBlockIrqPriority);

  while (!stopping_.load(std::memory_order_acquire)) {
    zx_signals_t observed = 0;
    zx_status_t status = completion_event_.wait_one(
        ZX_USER_SIGNAL_0, zx::time::infinite(), &observed);
    if (stopping_.load(std::memory_order_acquire)) {
      break;
    }
    if (status != ZX_OK) {
      continue;
    }
    completion_event_.signal(ZX_USER_SIGNAL_0, 0);
    if (!WaitForGuestCompletionDrain()) {
      NotifyGuest();
      continue;
    }
    uint32_t log_count =
        skipped_irq_log_count_.fetch_add(1, std::memory_order_relaxed);
    skipped_irq_count_.fetch_add(1, std::memory_order_relaxed);
    if (log_count < kPvBlockHotPathLogLimit) {
      FXL_VLOG(2) << "pvblk skip empty completion irq vmid=" << backend_vmid_
                  << " irq=" << irq_
                  << " count="
                  << skipped_irq_count_.load(std::memory_order_relaxed);
    }
  }
  return 0;
}

}  // namespace machina
