// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/arraysize.h"
#include "mock/mock_mmio.h"
#include "power_manager.h"
#include "registers.h"
#include "gtest/gtest.h"

class TestPowerManager {
public:
    void MockEnable()
    {
        auto reg_io = std::make_unique<magma::RegisterIo>(MockMmio::Create(1024 * 1024));
        auto power_manager = std::make_unique<PowerManager>(reg_io.get());

        constexpr uint32_t kShaderOnOffset =
            static_cast<uint32_t>(registers::CoreReadyState::CoreType::kShader) +
            static_cast<uint32_t>(registers::CoreReadyState::ActionType::kActionPowerOn);
        constexpr uint32_t kShaderOnHighOffset = kShaderOnOffset + 4;
        constexpr uint32_t kDummyHighValue = 1500;
        reg_io->Write32(kShaderOnHighOffset, kDummyHighValue);
        power_manager->EnableCores(reg_io.get(), 0xf);
        // Higher word shouldn't be written to because none of them are being
        // enabled.
        EXPECT_EQ(kDummyHighValue, reg_io->Read32(kShaderOnHighOffset));

        registers::CoreReadyState::CoreType actions[] = {
            registers::CoreReadyState::CoreType::kShader, registers::CoreReadyState::CoreType::kL2,
            registers::CoreReadyState::CoreType::kTiler};
        for (size_t i = 0; i < arraysize(actions); i++) {

            uint32_t offset =
                static_cast<uint32_t>(actions[i]) +
                static_cast<uint32_t>(registers::CoreReadyState::ActionType::kActionPowerOn);

            if (actions[i] == registers::CoreReadyState::CoreType::kShader)
                EXPECT_EQ(0xfu, reg_io->Read32(offset));
            else
                EXPECT_EQ(1u, reg_io->Read32(offset));
        }
    }

    void TimeCoalesce()
    {
        auto reg_io = std::make_unique<magma::RegisterIo>(MockMmio::Create(1024 * 1024));
        PowerManager power_manager(reg_io.get());

        for (int i = 0; i < 100; i++) {
            power_manager.UpdateGpuActive(true);
            usleep(1000);
            power_manager.UpdateGpuActive(false);
            usleep(1000);
        }

        auto time_periods = power_manager.time_periods();
        EXPECT_GE(3u, time_periods.size());
    }
};

TEST(PowerManager, MockEnable)
{
    TestPowerManager test;
    test.MockEnable();
}

TEST(PowerManager, TimeAccumulation)
{
    auto reg_io = std::make_unique<magma::RegisterIo>(MockMmio::Create(1024 * 1024));
    PowerManager power_manager(reg_io.get());
    power_manager.UpdateGpuActive(true);
    usleep(150 * 1000);

    std::chrono::steady_clock::duration total_time;
    std::chrono::steady_clock::duration active_time;
    power_manager.GetGpuActiveInfo(&total_time, &active_time);
    EXPECT_LE(100u, std::chrono::duration_cast<std::chrono::milliseconds>(total_time).count());
    EXPECT_EQ(total_time, active_time);
}

TEST(PowerManager, TimeCoalesce)
{
    TestPowerManager test;
    test.TimeCoalesce();
}
