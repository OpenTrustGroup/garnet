// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hevcdec.h"

#include <ddk/io-buffer.h>
#include <zircon/assert.h>

#include <algorithm>

#include "macros.h"
#include "memory_barriers.h"
#include "video_decoder.h"

zx_status_t HevcDec::LoadFirmware(const uint8_t* data, uint32_t size) {
  HevcMpsr::Get().FromValue(0).WriteTo(mmio()->dosbus);
  HevcCpsr::Get().FromValue(0).WriteTo(mmio()->dosbus);
  io_buffer_t firmware_buffer;
  const uint32_t kFirmwareSize = 4 * 4096;
  // Most buffers should be 64-kbyte aligned.
  const uint32_t kBufferAlignShift = 16;
  zx_status_t status = io_buffer_init_aligned(&firmware_buffer, owner_->bti(),
                                              kFirmwareSize, kBufferAlignShift,
                                              IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed to make firmware buffer");
    return status;
  }

  memcpy(io_buffer_virt(&firmware_buffer), data, std::min(size, kFirmwareSize));
  io_buffer_cache_flush(&firmware_buffer, 0, kFirmwareSize);

  BarrierAfterFlush();
  HevcImemDmaAdr::Get()
      .FromValue(truncate_to_32(io_buffer_phys(&firmware_buffer)))
      .WriteTo(mmio()->dosbus);
  HevcImemDmaCount::Get()
      .FromValue(kFirmwareSize / sizeof(uint32_t))
      .WriteTo(mmio()->dosbus);
  HevcImemDmaCtrl::Get().FromValue(0x8000 | (7 << 16)).WriteTo(mmio()->dosbus);

  if (!WaitForRegister(std::chrono::seconds(1), [this] {
        return (HevcImemDmaCtrl::Get().ReadFrom(mmio()->dosbus).reg_value() &
                0x8000) == 0;
      })) {
    DECODE_ERROR("Failed to load microcode.");

    BarrierBeforeRelease();
    io_buffer_release(&firmware_buffer);
    return ZX_ERR_TIMED_OUT;
  }

  BarrierBeforeRelease();
  io_buffer_release(&firmware_buffer);
  return ZX_OK;
}

void HevcDec::PowerOn() {
  {
    auto temp = AoRtiGenPwrSleep0::Get().ReadFrom(mmio()->aobus);
    temp.set_reg_value(temp.reg_value() & ~0xc0);
    temp.WriteTo(mmio()->aobus);
  }
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));

  DosSwReset3::Get().FromValue(0xffffffff).WriteTo(mmio()->dosbus);
  DosSwReset3::Get().FromValue(0).WriteTo(mmio()->dosbus);

  owner_->UngateClocks();

  enum {
    kGxmFclkDiv4 = 0,  // 500 MHz
    kGxmFclkDiv3 = 1,  // 666 MHz
    kGxmFclkDiv5 = 2,  // 400 MHz
    kGxmFclkDiv7 = 3,  // 285.7 MHz
    kGxmMp1 = 4,
    kGxmMp2 = 5,
    kGxmGp0 = 6,
    kGxmXtal = 7,  // 24 MHz

    kG12aFclkDiv2p5 = 0,  // 800 MHz
    kG12aFclkDiv3 = 1,    // 666 MHz
    kG12aFclkDiv4 = 2,    // 500 MHz
    kG12aFclkDiv5 = 3,    // 400 MHz
    kG12aFclkDiv7 = 4,    // 285.7 MHz
    kG12aHifi = 5,
    kG12aGp0 = 6,
    kG12aXtal = 7,  // 24 MHz
  };

  // Pick 500 MHz. The maximum frequency used in linux is 648 MHz, but that
  // requires using GP0, which is already being used by the GPU.
  // The linux driver also uses 200MHz in some circumstances for videos <=
  // 1080p30.
  uint32_t clock_sel =
      owner_->device_type() == DeviceType::kG12A ? kG12aFclkDiv4 : kGxmFclkDiv4;

  auto clock_cntl =
      HhiHevcClkCntl::Get().FromValue(0).set_vdec_en(true).set_vdec_sel(
          clock_sel);
  // GXM HEVC doesn't have a front half.
  if (owner_->device_type() == DeviceType::kG12A) {
    clock_cntl.set_front_enable(true).set_front_sel(clock_sel);
  }
  clock_cntl.WriteTo(mmio()->hiubus);
  DosGclkEn3::Get().FromValue(0xffffffff).WriteTo(mmio()->dosbus);
  DosMemPdHevc::Get().FromValue(0).WriteTo(mmio()->dosbus);
  {
    auto temp = AoRtiGenPwrIso0::Get().ReadFrom(mmio()->aobus);
    temp.set_reg_value(temp.reg_value() & ~0xc00);
    temp.WriteTo(mmio()->aobus);
  }

  DosSwReset3::Get().FromValue(0xffffffff).WriteTo(mmio()->dosbus);
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
  DosSwReset3::Get().FromValue(0).WriteTo(mmio()->dosbus);
  powered_on_ = true;
}

void HevcDec::PowerOff() {
  if (!powered_on_)
    return;
  powered_on_ = false;
  {
    auto temp = AoRtiGenPwrIso0::Get().ReadFrom(mmio()->aobus);
    temp.set_reg_value(temp.reg_value() | 0xc00);
    temp.WriteTo(mmio()->aobus);
  }
  // Power down internal memory
  DosMemPdHevc::Get().FromValue(0xffffffffu).WriteTo(mmio()->dosbus);

  // Disable clocks
  HhiHevcClkCntl::Get()
      .FromValue(0)
      .set_vdec_en(false)
      .set_vdec_sel(3)
      .set_front_enable(false)
      .set_front_sel(3)
      .WriteTo(mmio()->hiubus);
  // Turn off power gates
  {
    auto temp = AoRtiGenPwrSleep0::Get().ReadFrom(mmio()->aobus);
    temp.set_reg_value(temp.reg_value() | 0xc0);
    temp.WriteTo(mmio()->aobus);
  }
  owner_->GateClocks();
}

void HevcDec::StartDecoding() {
  assert(!decoding_started_);
  decoding_started_ = true;
  // Delay to wait for previous command to finish.
  for (uint32_t i = 0; i < 3; i++) {
    DosSwReset3::Get().ReadFrom(mmio()->dosbus);
  }

  DosSwReset3::Get().FromValue(0).set_mcpu(1).set_ccpu(1).WriteTo(
      mmio()->dosbus);
  DosSwReset3::Get().FromValue(0).WriteTo(mmio()->dosbus);

  // Delay to wait for previous command to finish.
  for (uint32_t i = 0; i < 3; i++) {
    DosSwReset3::Get().ReadFrom(mmio()->dosbus);
  }

  HevcMpsr::Get().FromValue(1).WriteTo(mmio()->dosbus);
}

void HevcDec::StopDecoding() {
  if (!decoding_started_)
    return;
  decoding_started_ = false;
  HevcMpsr::Get().FromValue(0).WriteTo(mmio()->dosbus);
  HevcCpsr::Get().FromValue(0).WriteTo(mmio()->dosbus);

  if (!WaitForRegister(std::chrono::seconds(1), [this] {
        return (HevcImemDmaCtrl::Get().ReadFrom(mmio()->dosbus).reg_value() &
                0x8000) == 0;
      })) {
    DECODE_ERROR("Failed to wait for DMA completion");
    return;
  }
  // Delay to wait for previous command to finish.
  for (uint32_t i = 0; i < 3; i++) {
    DosSwReset3::Get().ReadFrom(mmio()->dosbus);
  }
}

void HevcDec::WaitForIdle() {
  auto timeout = std::chrono::milliseconds(100);
  if (!WaitForRegister(timeout, [this] {
        return HevcMdecPicDcStatus::Get()
                   .ReadFrom(mmio()->dosbus)
                   .reg_value() == 0;
      })) {
    // Forcibly shutoff video output hardware. Probably.
    auto temp = HevcMdecPicDcCtrl::Get().ReadFrom(mmio()->dosbus);
    temp.set_reg_value(1 | temp.reg_value());
    temp.WriteTo(mmio()->dosbus);
    temp.set_reg_value(~1 & temp.reg_value());
    temp.WriteTo(mmio()->dosbus);
    for (uint32_t i = 0; i < 3; i++) {
      HevcMdecPicDcStatus::Get().ReadFrom(mmio()->dosbus);
    }
  }
  if (!WaitForRegister(timeout, [this] {
        return !(HevcDblkStatus::Get().ReadFrom(mmio()->dosbus).reg_value() &
                 1);
      })) {
    // Forcibly shutoff deblocking hardware.
    HevcDblkCtrl::Get().FromValue(3).WriteTo(mmio()->dosbus);
    HevcDblkCtrl::Get().FromValue(0).WriteTo(mmio()->dosbus);
    for (uint32_t i = 0; i < 3; i++) {
      HevcDblkStatus::Get().ReadFrom(mmio()->dosbus);
    }
  }

  WaitForRegister(timeout, [this] {
    return !(HevcDcacDmaCtrl::Get().ReadFrom(mmio()->dosbus).reg_value() &
             0x8000);
  });
}

void HevcDec::InitializeStreamInput(bool use_parser, uint32_t buffer_address,
                                    uint32_t buffer_size) {
  HevcStreamControl::Get()
      .ReadFrom(mmio()->dosbus)
      .set_stream_fetch_enable(false)
      .WriteTo(mmio()->dosbus);
  HevcStreamStartAddr::Get().FromValue(buffer_address).WriteTo(mmio()->dosbus);
  HevcStreamEndAddr::Get()
      .FromValue(buffer_address + buffer_size)
      .WriteTo(mmio()->dosbus);
  HevcStreamRdPtr::Get().FromValue(buffer_address).WriteTo(mmio()->dosbus);
  HevcStreamWrPtr::Get().FromValue(buffer_address).WriteTo(mmio()->dosbus);
}

void HevcDec::InitializeParserInput() {
  DosGenCtrl0::Get()
      .FromValue(0)
      .set_vbuf_rp_select(DosGenCtrl0::kHevc)
      .WriteTo(mmio()->dosbus);
  HevcStreamControl::Get()
      .ReadFrom(mmio()->dosbus)
      .set_endianness(0)
      .set_use_parser_vbuf_wp(true)
      .set_stream_fetch_enable(true)
      .WriteTo(mmio()->dosbus);
  HevcStreamFifoCtl::Get()
      .ReadFrom(mmio()->dosbus)
      .set_stream_fifo_hole(1)
      .WriteTo(mmio()->dosbus);
}

void HevcDec::InitializeDirectInput() {
  HevcStreamControl::Get()
      .ReadFrom(mmio()->dosbus)
      .set_endianness(7)
      .set_use_parser_vbuf_wp(false)
      .set_stream_fetch_enable(false)
      .WriteTo(mmio()->dosbus);
  HevcStreamFifoCtl::Get()
      .ReadFrom(mmio()->dosbus)
      .set_stream_fifo_hole(1)
      .WriteTo(mmio()->dosbus);
}

void HevcDec::UpdateWritePointer(uint32_t write_pointer) {
  HevcStreamWrPtr::Get().FromValue(write_pointer).WriteTo(mmio()->dosbus);
  HevcStreamControl::Get()
      .ReadFrom(mmio()->dosbus)
      .set_endianness(7)
      .set_use_parser_vbuf_wp(false)
      .set_stream_fetch_enable(true)
      .WriteTo(mmio()->dosbus);
}

uint32_t HevcDec::GetStreamInputOffset() {
  uint32_t write_ptr =
      HevcStreamWrPtr::Get().ReadFrom(mmio()->dosbus).reg_value();
  uint32_t buffer_start =
      HevcStreamStartAddr::Get().ReadFrom(mmio()->dosbus).reg_value();
  assert(write_ptr >= buffer_start);
  return write_ptr - buffer_start;
}

uint32_t HevcDec::GetReadOffset() {
  uint32_t read_ptr =
      HevcStreamRdPtr::Get().ReadFrom(mmio()->dosbus).reg_value();
  uint32_t buffer_start =
      HevcStreamStartAddr::Get().ReadFrom(mmio()->dosbus).reg_value();
  assert(read_ptr >= buffer_start);
  return read_ptr - buffer_start;
}

zx_status_t HevcDec::InitializeInputContext(InputContext* context) {
  constexpr uint32_t kInputContextSize = 4096;
  zx_status_t status =
      io_buffer_init_aligned(&context->buffer, owner_->bti(), kInputContextSize,
                             0, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    DECODE_ERROR("Failed to allocate input context, status %d\n", status);
    return status;
  }
  io_buffer_cache_flush(&context->buffer, 0, kInputContextSize);
  BarrierAfterFlush();
  return status;
}

void HevcDec::SaveInputContext(InputContext* context) {
  HevcStreamSwapAddr::Get()
      .FromValue(truncate_to_32(io_buffer_phys(&context->buffer)))
      .WriteTo(mmio()->dosbus);
  HevcStreamSwapCtrl::Get()
      .FromValue(0)
      .set_enable(true)
      .set_save(true)
      .WriteTo(mmio()->dosbus);
  bool finished = SpinWaitForRegister(std::chrono::milliseconds(100), [this]() {
    return !HevcStreamSwapCtrl::Get().ReadFrom(mmio()->dosbus).in_progress();
  });
  // TODO: return error on failure.
  ZX_ASSERT(finished);
  HevcStreamSwapCtrl::Get().FromValue(0).WriteTo(mmio()->dosbus);

  context->processed_video =
      HevcShiftByteCount::Get().ReadFrom(mmio()->dosbus).reg_value();
}

void HevcDec::RestoreInputContext(InputContext* context) {
  // Stream fetching enabled needs to be set before the rest of the state is
  // restored, or else the parser's state becomes incorrect and decoding fails.
  HevcStreamControl::Get()
      .ReadFrom(mmio()->dosbus)
      .set_endianness(7)
      .set_use_parser_vbuf_wp(false)
      .set_stream_fetch_enable(true)
      .WriteTo(mmio()->dosbus);
  HevcStreamSwapAddr::Get()
      .FromValue(truncate_to_32(io_buffer_phys(&context->buffer)))
      .WriteTo(mmio()->dosbus);
  HevcStreamSwapCtrl::Get().FromValue(0).set_enable(true).WriteTo(
      mmio()->dosbus);
  bool finished = SpinWaitForRegister(std::chrono::milliseconds(100), [this]() {
    return !HevcStreamSwapCtrl::Get().ReadFrom(mmio()->dosbus).in_progress();
  });
  // TODO: return error on failure.
  ZX_ASSERT(finished);
  HevcStreamSwapCtrl::Get().FromValue(0).WriteTo(mmio()->dosbus);
}
