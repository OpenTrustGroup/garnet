// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGISTERS_H
#define REGISTERS_H

#include "magma_util/macros.h"
#include "magma_util/register_bitfields.h"
#include "magma_util/register_io.h"

namespace registers {

class ChipId : public magma::RegisterBase {
public:
    DEF_FIELD(31, 0, chip_id);

    static auto Get() { return magma::RegisterAddr<ChipId>(0x20); }
};

class Revision : public magma::RegisterBase {
public:
    DEF_FIELD(31, 0, chip_revision);

    static auto Get() { return magma::RegisterAddr<Revision>(0x24); }
};

class Features : public magma::RegisterBase {
public:
    DEF_BIT(0, fast_clear);
    DEF_BIT(1, special_anti_aliasing);
    DEF_BIT(2, pipe_3d);
    DEF_BIT(3, dxt_texture_compression);
    DEF_BIT(4, debug_mode);
    DEF_BIT(5, z_compression);
    DEF_BIT(6, yuv420_scaler);
    DEF_BIT(7, msaa);
    DEF_BIT(8, dc);
    DEF_BIT(9, pipe_2d);
    DEF_BIT(10, etc1_texture_compression);
    DEF_BIT(11, fast_scaler);
    DEF_BIT(12, high_dynamic_range);
    DEF_BIT(13, yuv420_tiler);
    DEF_BIT(14, module_cg);
    DEF_BIT(15, min_area);
    DEF_BIT(16, no_early_z);
    DEF_BIT(17, no_422_texture);
    DEF_BIT(18, buffer_interleaving);
    DEF_BIT(19, byte_write_2d);
    DEF_BIT(20, no_scaler);
    DEF_BIT(21, yuy2_averaging);
    DEF_BIT(22, half_pe_cache);
    DEF_BIT(23, half_tx_cache);
    DEF_BIT(24, yuy2_render_target);
    DEF_BIT(25, mem32);
    DEF_BIT(26, pipe_vg);
    DEF_BIT(27, vgts);
    DEF_BIT(28, fe20);
    DEF_BIT(29, byte_write_3d);
    DEF_BIT(30, rs_yuv_target);
    DEF_BIT(31, indices_32bit);

    static auto Get() { return magma::RegisterAddr<Features>(0x1C); }
};

class MinorFeatures : public magma::RegisterBase {
public:
    static constexpr uint32_t kMoreMinorFeatures = 1 << 21;

    static auto Get(uint32_t index)
    {
        switch (index) {
            case 0:
                return magma::RegisterAddr<MinorFeatures>(0x34);
            case 1:
                return magma::RegisterAddr<MinorFeatures>(0x74);
            case 2:
                return magma::RegisterAddr<MinorFeatures>(0x84);
            case 3:
                return magma::RegisterAddr<MinorFeatures>(0x88);
            case 4:
                return magma::RegisterAddr<MinorFeatures>(0x94);
            case 5:
                return magma::RegisterAddr<MinorFeatures>(0xA0);
        }
        DASSERT(false);
        return magma::RegisterAddr<MinorFeatures>(0x0);
    }
};

class Specs1 : public magma::RegisterBase {
public:
    DEF_FIELD(3, 0, stream_count);
    DEF_FIELD(7, 4, log2_register_max);
    DEF_FIELD(11, 8, log2_thread_count);
    DEF_FIELD(16, 12, vertex_cache_size);
    DEF_FIELD(24, 20, shader_core_count);
    DEF_FIELD(27, 25, pixel_pipes);
    DEF_FIELD(31, 28, log2_vertex_output_buffer_size);

    static auto Get() { return magma::RegisterAddr<Specs1>(0x48); }
};

class Specs2 : public magma::RegisterBase {
public:
    DEF_FIELD(7, 0, buffer_size);
    DEF_FIELD(15, 8, instruction_count);
    DEF_FIELD(31, 16, num_constants);

    static auto Get() { return magma::RegisterAddr<Specs2>(0x80); }
};

class Specs3 : public magma::RegisterBase {
public:
    DEF_FIELD(8, 4, varyings_count);

    static auto Get() { return magma::RegisterAddr<Specs3>(0x8C); }
};

class Specs4 : public magma::RegisterBase {
public:
    DEF_FIELD(16, 12, stream_count);

    static auto Get() { return magma::RegisterAddr<Specs4>(0x9C); }
};

class IdleState : public magma::RegisterBase {
public:
    bool IsIdle() { return reg_value() == 0x7fffffff; }

    static auto Get() { return magma::RegisterAddr<IdleState>(0x4); }
};

class SecureCommandControl : public magma::RegisterBase {
public:
    DEF_FIELD(15, 0, prefetch);
    DEF_BIT(16, enable);

    static auto Get() { return magma::RegisterAddr<SecureCommandControl>(0x3A4); }
};

class SecureAhbControl : public magma::RegisterBase {
public:
    DEF_BIT(1, non_secure_access);

    static auto Get() { return magma::RegisterAddr<SecureAhbControl>(0x3A8); }
};

class FetchEngineCommandAddress : public magma::RegisterBase {
public:
    DEF_FIELD(31, 0, addr);

    static auto Get() { return magma::RegisterAddr<FetchEngineCommandAddress>(0x654); }
};

class FetchEngineCommandControl : public magma::RegisterBase {
public:
    DEF_FIELD(15, 0, prefetch);
    DEF_BIT(16, enable);

    static auto Get() { return magma::RegisterAddr<FetchEngineCommandControl>(0x658); }
};

class DmaAddress : public magma::RegisterBase {
public:
    static auto Get() { return magma::RegisterAddr<DmaAddress>(0x664); }
};

} // namespace registers

#endif // REGISTERS_H
