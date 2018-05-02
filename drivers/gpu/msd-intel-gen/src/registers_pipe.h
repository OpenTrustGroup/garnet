// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGISTERS_PIPE_H
#define REGISTERS_PIPE_H

#include "magma_util/register_bitfields.h"

// Registers for controlling the pipes, including planes (which are part of
// pipes).

namespace registers {

// PIPE_SRCSZ: Source image size for pipe.
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf
class PipeSourceSize : public magma::RegisterBase {
public:
    static constexpr uint32_t kBaseAddr = 0x6001C;

    DEF_FIELD(28, 16, horizontal_source_size);
    DEF_FIELD(11, 0, vertical_source_size);
};

class PipeScalerControl : public magma::RegisterBase {
public:
    static constexpr uint32_t kBaseAddr = 0x68180;

    enum Mode { DYNAMIC = 0, SEVEN_BY_FIVE = 1 };
    enum Binding { PIPE = 0, PLANE_1 = 1, PLANE_2 = 2, PLANE_3 = 3, PLANE_4 = 4 };
    enum Filter { MEDIUM = 0, EDGE_ENHANCE = 2, BILINEAR = 3 };

    DEF_BIT(31, enable);
    DEF_FIELD(29, 28, mode);
    DEF_FIELD(27, 25, binding);
    DEF_FIELD(24, 23, filter_select);
};

class PipeScalerWindowSize : public magma::RegisterBase {
public:
    static constexpr uint32_t kBaseAddr = 0x68174;

    DEF_FIELD(28, 16, x_size);
    DEF_FIELD(11, 0, y_size);
};

// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf p.601
class PlaneSurfaceAddress : public magma::RegisterBase {
public:
    static constexpr uint32_t kBaseAddr = 0x7019C;

    // This field omits the lower 12 bits of the address, so the address
    // must be 4k-aligned.
    static constexpr uint32_t kPageShift = 12;
    DEF_FIELD(31, 12, surface_base_address);

    DEF_BIT(3, ring_flip_source);
};

// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf p.598
class PlaneSurfaceStride : public magma::RegisterBase {
public:
    static constexpr uint32_t kBaseAddr = 0x70188;

    DEF_FIELD(9, 0, stride);
};

class PlaneSurfaceSize : public magma::RegisterBase {
public:
    static constexpr uint32_t kBaseAddr = 0x70190;

    DEF_FIELD(27, 16, height_minus_1);
    DEF_FIELD(12, 0, width_minus_1);
};

// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf p.559-566
class PlaneControl : public magma::RegisterBase {
public:
    static constexpr uint32_t kBaseAddr = 0x70180;

    DEF_BIT(31, plane_enable);
    DEF_BIT(30, pipe_gamma_enable);
    DEF_BIT(29, remove_yuv_offset);
    DEF_BIT(28, yuv_range_correction_disable);

    DEF_FIELD(27, 24, source_pixel_format);
    static constexpr uint32_t kFormatRgb8888 = 4;

    DEF_BIT(23, pipe_csc_enable);
    DEF_FIELD(22, 21, key_enable);
    DEF_BIT(20, rgb_color_order);
    DEF_BIT(19, plane_yuv_to_rgb_csc_dis);
    DEF_BIT(18, plane_yuv_to_rgb_csc_format);
    DEF_FIELD(17, 16, yuv_422_byte_order);
    DEF_BIT(15, render_decompression);
    DEF_BIT(14, trickle_feed_enable);
    DEF_BIT(13, plane_gamma_disable);

    DEF_FIELD(12, 10, tiled_surface);
    enum Tiling { TILING_NONE = 0, TILING_X = 1, TILING_Y_LEGACY = 4, TILING_YF = 5 };

    DEF_BIT(9, async_address_update_enable);
    DEF_FIELD(7, 6, stereo_surface_vblank_mask);
    DEF_FIELD(5, 4, alpha_mode);
    DEF_BIT(3, allow_double_buffer_update_disable);
    DEF_FIELD(1, 0, plane_rotation);
};

// PLANE_BUF_CFG: Range of buffer used temporarily during scanout.
// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part2.pdf
class PlaneBufCfg : public magma::RegisterBase {
public:
    static constexpr uint32_t kBaseAddr = 0x7027C;

    DEF_FIELD(25, 16, buffer_end);
    DEF_FIELD(9, 0, buffer_start);
};

// from intel-gfx-prm-osrc-skl-vol02c-commandreference-registers-part1.pdf p.444
class DisplayPipeInterrupt {
public:
    enum Pipe { PIPE_A };

    static constexpr uint32_t kMaskOffsetPipeA = 0x44404;
    static constexpr uint32_t kIdentityOffsetPipeA = 0x44408;
    static constexpr uint32_t kEnableOffsetPipeA = 0x4440C;
    static constexpr uint32_t kPlane1FlipDoneBit = 1 << 3;

    static void write_mask(magma::RegisterIo* reg_io, Pipe pipe, uint32_t bits, bool enable)
    {
        uint32_t offset, val;
        switch (pipe) {
            case PIPE_A:
                offset = kMaskOffsetPipeA;
                break;
        }

        val = reg_io->Read32(offset);
        val = enable ? (val & ~bits) : val | bits;
        reg_io->Write32(offset, val);
    }

    static void write_enable(magma::RegisterIo* reg_io, Pipe pipe, uint32_t bits, bool enable)
    {
        uint32_t offset, val;
        switch (pipe) {
            case PIPE_A:
                offset = kEnableOffsetPipeA;
                break;
        }

        val = reg_io->Read32(offset);
        val = enable ? (val | bits) : (val & ~bits);
        reg_io->Write32(offset, val);
    }

    static void process_identity_bits(magma::RegisterIo* reg_io, Pipe pipe, uint32_t bits,
                                      bool* bits_present_out)
    {
        uint32_t offset, val;
        switch (pipe) {
            case PIPE_A:
                offset = kIdentityOffsetPipeA;
                break;
        }
        val = reg_io->Read32(offset);
        if ((*bits_present_out = val & bits))
            reg_io->Write32(offset, val | bits); // reset the event
    }
};

// An instance of PipeRegs represents the registers for a particular pipe.
class PipeRegs {
public:
    // Number of pipes that the hardware provides.
    static constexpr uint32_t kPipeCount = 3;

    PipeRegs(uint32_t pipe_number) : pipe_number_(pipe_number)
    {
        DASSERT(pipe_number < kPipeCount);
    }

    auto PipeSourceSize() { return GetReg<registers::PipeSourceSize>(); }
    auto PipeScalerControl() { return GetReg<registers::PipeScalerControl>(); }
    auto PipeScalerWindowSize() { return GetReg<registers::PipeScalerWindowSize>(); }

    // The following methods get the instance of the plane register for
    // Plane 1 of this pipe.
    auto PlaneSurfaceAddress() { return GetReg<registers::PlaneSurfaceAddress>(); }
    auto PlaneSurfaceStride() { return GetReg<registers::PlaneSurfaceStride>(); }
    auto PlaneSurfaceSize() { return GetReg<registers::PlaneSurfaceSize>(); }
    auto PlaneControl() { return GetReg<registers::PlaneControl>(); }
    auto PlaneBufCfg() { return GetReg<registers::PlaneBufCfg>(); }

private:
    template <class RegType> magma::RegisterAddr<RegType> GetReg()
    {
        return magma::RegisterAddr<RegType>(RegType::kBaseAddr + 0x1000 * pipe_number_);
    }

    uint32_t pipe_number_;
};

} // namespace

#endif // REGISTERS_PIPE_H
