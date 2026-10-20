// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/watchdog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <threads.h>
#include <sync/completion.h>
#include <zircon/driver/binding.h>
#include <zircon/types.h>
#include <zircon/threads.h>
#include <ddk/io-buffer.h>
#include "zx_wdt.h"
#include "mtk_wdt.h"
#include <hw/reg.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include <zircon/device/grt-wdt.h>

/****************************************/

#define WDT_MAX_TIMEOUT		31
#define WDT_MIN_TIMEOUT		2
#define WDT_LENGTH_TIMEOUT(n)	((n) << 5)

#define WDT_LENGTH		0x04
#define WDT_LENGTH_KEY		0x8

#define WDT_RST			0x08
#define WDT_RST_RELOAD		0x1971

#define WDT_MODE		0x00
#define WDT_MODE_EN		(1 << 0)
#define WDT_MODE_EXT_POL_LOW	(0 << 1)
#define WDT_MODE_EXT_POL_HIGH	(1 << 1)
#define WDT_MODE_EXRST_EN	(1 << 2)
#define WDT_MODE_IRQ_EN		(1 << 3)
#define WDT_MODE_AUTO_START	(1 << 4)
#define WDT_MODE_DUAL_EN	(1 << 6)
#define WDT_MODE_KEY		0x22000000

#define WDT_STATUS		0x0C
#define WDT_STATUS_SWWDT_RST	(1 << 30)

#define WDT_NONRST_REG		0x20


#define WDT_SWRST		0x14
#define WDT_SWRST_KEY		0x1209

#define WDT_SWSYSRST		0x18U
#define WDT_SWSYS_RST_KEY	0x88000000

#define DRV_NAME		"mtk-wdt"
#define DRV_VERSION		"1.0"

#define DEF_TIMEOUT      15
static const unsigned int HW_TIMEOUT = 30;
#define TUNE_TIME      14
static const uint32_t kGuestManagerHeartbeatIntervalSecDefault = 10;
static const uint32_t kGuestManagerHeartbeatMissThresholdDefault = 3;
static const uint32_t kGuestManagerWatchdogBootGraceSecDefault = 30;

static zx_time_t monotonic_now(void)
{
    return zx_clock_get(ZX_CLOCK_MONOTONIC);
}

#define PSCI_1_1_RESET2_TYPE_VENDOR_SHIFT   31U
#define PSCI_1_1_RESET2_TYPE_VENDOR     \
    (1ULL << PSCI_1_1_RESET2_TYPE_VENDOR_SHIFT)

#define RESET2_TYPE_DOMAIN_SHIFT	0
#define RESET2_TYPE_DOMAIN_USAGE_SHIFT	8U
#define RESET2_TYPE_VENDOR		PSCI_1_1_RESET2_TYPE_VENDOR_SHIFT

#define PSCI_FN_NATIVE(version, name)   PSCI_##version##_FN64_##name
#define PSCI64_PSCI_WDT_REBOOT 0xC4000012
#define MTK_DOMAIN_AEE  1
#define CPU_STATS_FLAG_ONLINE       (1u<<0)

static int enable_mtk_wdt = 1;
static const int32_t kNorPriority = 16;
static const int32_t kWDTPriority = kNorPriority + 2;

enum REBOOT_MODE {
    AEE_REBOOT_MODE_NORMAL = 0,
    AEE_REBOOT_MODE_KERNEL_OOPS,
    AEE_REBOOT_MODE_KERNEL_PANIC,
    AEE_REBOOT_MODE_NESTED_EXCEPTION,
    AEE_REBOOT_MODE_WDT,
    AEE_REBOOT_MODE_MANUAL_KDUMP,
    AEE_REBOOT_MODE_MRDUMP_KEY,
    AEE_REBOOT_MODE_GZ_KE,
    AEE_REBOOT_MODE_GZ_WDT,
    AEE_REBOOT_MODE_HANG_DETECT,
};

enum AEE_EXP_TYPE_NUM {
    AEE_EXP_TYPE_HWT = 1,
    AEE_EXP_TYPE_KE = 2,
    AEE_EXP_TYPE_NESTED_PANIC = 3,
    AEE_EXP_TYPE_SMART_RESET = 4,
    AEE_EXP_TYPE_HANG_DETECT = 5,
    AEE_EXP_TYPE_BL33_CRASH = 6,
    AEE_EXP_TYPE_BL2_EXT_CRASH = 7,
    AEE_EXP_TYPE_AEE_LK_CRASH = 8,
    AEE_EXP_TYPE_TFA_CRASH = 9,
    AEE_EXP_TYPE_PL_CRASH = 10,
    AEE_EXP_TYPE_GZ_CRASH = 11,
    AEE_EXP_TYPE_NBL_HWT = 13,
    AEE_EXP_TYPE_MAX_NUM = 16,
};

static void exception_reboot(int reboot_reason)
{
    uint64_t opt1 = 0;
    zx_smc_result_t result;
    zx_status_t status;
    zx_smc_parameters_t parameters = {0};

    if (reboot_reason == AEE_REBOOT_MODE_HANG_DETECT)
        opt1 |= ((unsigned char)AEE_EXP_TYPE_HANG_DETECT) <<
                RESET2_TYPE_DOMAIN_USAGE_SHIFT;
    else if (reboot_reason == AEE_REBOOT_MODE_WDT)
        opt1 |= ((unsigned char)AEE_EXP_TYPE_NBL_HWT) <<
                RESET2_TYPE_DOMAIN_USAGE_SHIFT;
    else
        opt1 |= ((unsigned char)AEE_EXP_TYPE_KE) <<
                RESET2_TYPE_DOMAIN_USAGE_SHIFT;

    opt1 |= (unsigned char)MTK_DOMAIN_AEE;
    // arm_smccc_smc(PSCI64_PSCI_WDT_REBOOT,
    // 	PSCI_1_1_RESET2_TYPE_VENDOR | opt1,
    // 	opt2, 0, 0, 0, 0, 0, &res);

    parameters.func_id = PSCI64_PSCI_WDT_REBOOT;
    parameters.arg1 = PSCI_1_1_RESET2_TYPE_VENDOR | opt1;
    parameters.arg2 = 0;
    parameters.arg3 = 0;
    parameters.arg4 = 0;
    parameters.arg5 = 0;
    parameters.arg6 = 0;
    parameters.client_id = 0;
    parameters.secure_os_id = 0;

    zxlogf(ERROR, "event=platform_wdt_reset_request reason=%d smc=0x%x arg1=0x%" PRIx64 "\n",
           reboot_reason, parameters.func_id, parameters.arg1);
    status = zx_smc_call(get_root_resource(), &parameters, &result);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s Failed to send smcall %d\n", __func__, status);
        return;
    }
}

static int mtk_wdt_restart(void *ctx)
{
     mtk_wdt_t* wdt = ctx;

    if (!wdt)
         return ZX_ERR_INVALID_ARGS;

    //writel((MTK_WDT_RESTART_KEY), (wdt)->wdt_base + (MTK_WDT_RESTART));
    zxlogf(INFO, "%s:%d\n", __func__, __LINE__);
    while (1) {
        writel(WDT_SWRST_KEY, wdt->wdt_base + WDT_SWRST);
        zx_nanosleep(zx_deadline_after(ZX_MSEC(DEF_TIMEOUT)));
    }

    return 0;
}

static int mtk_wdt_sos_ping(void* ctx)
{
    mtk_wdt_t* wdt = ctx;
    if (!wdt) {
        return ZX_ERR_INVALID_ARGS;
    }
    mtx_lock(&wdt->lock);
    const uint64_t now = zx_ticks_get();
    const bool accepted = wdt_supervision_kick(&wdt->supervision, now);
    if (accepted) {
        wdt->sos_hartbit_time = now;
    }
    mtx_unlock(&wdt->lock);
    return accepted ? ZX_OK : ZX_ERR_BAD_STATE;
}

static int mtk_wdt_request_reset(void* ctx)
{
    mtk_wdt_t* wdt = ctx;
    if (!wdt) {
        return ZX_ERR_INVALID_ARGS;
    }
    mtx_lock(&wdt->lock);
    const bool first = wdt_supervision_fail(&wdt->supervision);
    mtx_unlock(&wdt->lock);
    if (!first) {
        return ZX_ERR_BAD_STATE;
    }
    // Firmware may not return; no driver mutex is held across the SMC.
    // If it returns, the reset latch prevents every future hardware reload.
    exception_reboot(AEE_REBOOT_MODE_WDT);
    return ZX_OK;
}

static int mtk_wdt_set_sos_timeout(void* ctx, uint32_t timeout)
{
    mtk_wdt_t* wdt = ctx;
    if (!wdt || timeout < WDT_MIN_TIMEOUT || timeout > WDT_MAX_TIMEOUT) {
        return ZX_ERR_INVALID_ARGS;
    }
    mtx_lock(&wdt->lock);
    if (wdt->supervision.reset_latched) {
        mtx_unlock(&wdt->lock);
        return ZX_ERR_BAD_STATE;
    }
    wdt->supervision.sos_timeout = (uint64_t)timeout * 2 * zx_ticks_per_second();
    mtx_unlock(&wdt->lock);
    return ZX_OK;
}

static void mtk_wdt_refresh_timeout_baseline_locked(mtk_wdt_t* wdt,
                                                    uint64_t ticks_now,
                                                    zx_time_t monotonic_now_ns)
{
    for (uint32_t i = 0; i < MAX_PCPU; i++) {
        wdt->last_kick_time[i] = ticks_now;
    }

    wdt->nebula_kick_time = ticks_now;
    wdt->sos_hartbit_time = ticks_now;
    wdt_supervision_resume(&wdt->supervision, ticks_now);
    if (wdt->guest_manager_watchdog_enabled) {
        wdt->guest_manager_last_heartbeat = monotonic_now_ns;
        wdt->guest_manager_heartbeat_lost_logged = false;
    }
}

static int mtk_wdt_suspend(void *ctx)
{
    mtk_wdt_t* wdt = ctx;
    uint64_t ticks_now;
    zx_time_t monotonic_now_ns;

    if (!wdt)
        return ZX_ERR_INVALID_ARGS;

    ticks_now = zx_ticks_get();
    monotonic_now_ns = monotonic_now();
    mtx_lock(&wdt->lock);
    if (wdt->supervision.reset_latched) {
        mtx_unlock(&wdt->lock);
        return ZX_ERR_BAD_STATE;
    }
    wdt->bsuspend = true;
    mtk_wdt_refresh_timeout_baseline_locked(wdt, ticks_now, monotonic_now_ns);
    writel(WDT_RST_RELOAD, wdt->wdt_base + WDT_RST);
    mtx_unlock(&wdt->lock);

    zxlogf(INFO, "suspend.%s:%d\n", __func__, __LINE__);

    return 0;
}

static int mtk_wdt_resume(void *ctx)
{
    mtk_wdt_t* wdt = ctx;
    uint64_t ticks_now;
    zx_time_t monotonic_now_ns;

    if (!wdt)
        return ZX_ERR_INVALID_ARGS;

    ticks_now = zx_ticks_get();
    monotonic_now_ns = monotonic_now();
    mtx_lock(&wdt->lock);
    if (wdt->supervision.reset_latched) {
        mtx_unlock(&wdt->lock);
        return ZX_ERR_BAD_STATE;
    }
    mtk_wdt_refresh_timeout_baseline_locked(wdt, ticks_now, monotonic_now_ns);
    wdt->bsuspend = false;
    writel(WDT_RST_RELOAD, wdt->wdt_base + WDT_RST);
    mtx_unlock(&wdt->lock);

    zxlogf(INFO, "resume.%s:%d\n", __func__, __LINE__);

    return 0;
}

// Called with wdt->lock held; the only periodic hardware reload path.
static int mtk_wdt_ping(void *ctx)
{
    mtk_wdt_t* wdt = ctx;
    if (!wdt || wdt->supervision.reset_latched) {
        return ZX_ERR_BAD_STATE;
    }
    wdt->wdt_last_updata_time = zx_ticks_get();
    wdt->nebula_ping_count++;
    zxlogf(INFO,
           "event=nebula_watchdog_ping count=%" PRIu64
           " cpu=%u timeout_sec=%u now_ticks=%" PRIu64 "\n",
           wdt->nebula_ping_count, zx_thread_get_current_cpu(), wdt->timeout,
           zx_ticks_get());
    writel(WDT_RST_RELOAD, wdt->wdt_base + WDT_RST);
    return ZX_OK;
}

static int mtk_wdt_set_timeout(void *ctx,
                unsigned int timeout)
{
    mtk_wdt_t* wdt = ctx;
    uint32_t reg;

    if (!wdt)
         return ZX_ERR_INVALID_ARGS;

    zxlogf(TRACE, "%s:%d\n", __func__, __LINE__);
    wdt->timeout = timeout;
    /*
     * In dual mode, irq will be triggered at timeout / 2
     * the real timeout occurs at timeout
     */
    if (wdt->pretimeout)
        wdt->pretimeout = timeout / 2;

    /*
     * One bit is the value of 512 ticks
     * The clock has 32 KHz
     */
    reg = WDT_LENGTH_TIMEOUT((HW_TIMEOUT - wdt->pretimeout) << 6)
            | WDT_LENGTH_KEY;
    writel(reg, wdt->wdt_base + WDT_LENGTH);

    if (wdt->sos_hartbit_time == 0) {
        wdt->wdt_last_updata_time = wdt->sos_hartbit_time = zx_ticks_get();
    }

    return 0;
}

static void mtk_wdt_init(void *ctx)
{
    mtk_wdt_t* wdt = ctx;

    zxlogf(TRACE, "%s:%d\n", __func__, __LINE__);
    wdt->timeout = DEF_TIMEOUT;
    if (!wdt)
         return;

    if (readl(wdt->wdt_base + WDT_MODE) & WDT_MODE_EN) {
        mtk_wdt_set_timeout(wdt, wdt->timeout);
    }
}

static int mtk_wdt_stop(void *ctx)
{
    mtk_wdt_t* wdt = ctx;
    uint32_t reg;

    zxlogf(INFO, "%s:%d\n", __func__, __LINE__);
    if (!wdt)
        return ZX_ERR_INVALID_ARGS;

    wdt->bstop_sos = true;
    reg = readl(wdt->wdt_base + WDT_MODE);
    reg &= ~WDT_MODE_EN;
    reg |= WDT_MODE_KEY;
    writel(reg, wdt->wdt_base + WDT_MODE);

    return 0;
}

static int mtk_wdt_start(void *ctx)
{
    uint32_t reg;
    mtk_wdt_t* wdt = ctx;
    int ret;

    zxlogf(INFO, "%s:%d\n", __func__, __LINE__);
    if (!wdt)
         return ZX_ERR_INVALID_ARGS;

    wdt->bstop_sos = false;
    if (!wdt->timeout)
        wdt->timeout = DEF_TIMEOUT;

    ret = mtk_wdt_set_timeout(wdt, wdt->timeout);
    if (ret < 0)
        return ret;

    reg = readl(wdt->wdt_base + WDT_MODE);
    if (wdt->pretimeout)
        reg |= (WDT_MODE_IRQ_EN | WDT_MODE_DUAL_EN);
    else
        reg &= ~(WDT_MODE_IRQ_EN | WDT_MODE_DUAL_EN);
    reg |= (WDT_MODE_EN | WDT_MODE_KEY);
    writel(reg, wdt->wdt_base + WDT_MODE);

    return 0;
}

static int mtk_wdt_set_pretimeout(void *ctx,
                  unsigned int timeout)
{
    mtk_wdt_t* wdt = ctx;
    uint32_t reg;

    zxlogf(TRACE, "%s:%d\n", __func__, __LINE__);
    if (!wdt)
         return ZX_ERR_INVALID_ARGS;

    if (!wdt->timeout)
        wdt->timeout = DEF_TIMEOUT;

    reg = readl(wdt->wdt_base + WDT_MODE);

    if (timeout && !wdt->pretimeout) {
        wdt->pretimeout = wdt->timeout / 2;
        reg |= (WDT_MODE_IRQ_EN | WDT_MODE_DUAL_EN);
    } else if (!timeout && wdt->pretimeout) {
        wdt->pretimeout = 0;
        reg &= ~(WDT_MODE_IRQ_EN | WDT_MODE_DUAL_EN);
    } else {
        return 0;
    }

    reg |= WDT_MODE_KEY;
    writel(reg, wdt->wdt_base + WDT_MODE);

    return mtk_wdt_set_timeout(wdt, wdt->timeout);
}

// static const struct watchdog_ops mtk_wdt_ops = {
// 	.owner		= THIS_MODULE,
// 	.start		= mtk_wdt_start,
// 	.stop		= mtk_wdt_stop,
// 	.ping		= mtk_wdt_ping,
// 	.set_timeout	= mtk_wdt_set_timeout,
// 	.set_pretimeout	= mtk_wdt_set_pretimeout,
// 	.restart	= mtk_wdt_restart,
// };


static void mtk_wdt_set_sw_rst_status(void *ctx)
{
    mtk_wdt_t* wdt = ctx;
    uint32_t reg;

    if (!wdt) {
        zxlogf(ERROR, "get wdt failed.%s:%d\n", __func__, __LINE__);
        return;
    }

    if (!wdt->wdt_base) {
        zxlogf(ERROR, "get wdt base failed.%s:%d\n", __func__, __LINE__);
        return;
    }

    reg = readl(wdt->wdt_base + WDT_STATUS);
    reg |= WDT_STATUS_SWWDT_RST;
    zxlogf(INFO, "%s:%d, set reg:0x%x.\n", __func__, __LINE__, reg);

    writel(reg, wdt->wdt_base + WDT_STATUS);
    writel(reg, wdt->wdt_base + WDT_NONRST_REG);
}

static int mtk_wdt_set_gm_watchdog_config(void* ctx,
                                          const grt_gm_watchdog_config_t* config)
{
    mtk_wdt_t* wdt = ctx;
    zx_time_t now = monotonic_now();
    bool enabled;
    uint32_t interval_sec;
    uint32_t miss_threshold;
    uint32_t boot_grace_sec;

    if (!wdt || !config) {
        return ZX_ERR_INVALID_ARGS;
    }

    mtx_lock(&wdt->lock);
    if (wdt->supervision.reset_latched) {
        mtx_unlock(&wdt->lock);
        return ZX_ERR_BAD_STATE;
    }
    wdt->guest_manager_watchdog_enabled = config->enable != 0;
    wdt->guest_manager_heartbeat_interval_sec =
        config->heartbeat_interval_sec ? config->heartbeat_interval_sec
                                       : kGuestManagerHeartbeatIntervalSecDefault;
    wdt->guest_manager_heartbeat_miss_threshold =
        config->miss_threshold ? config->miss_threshold
                               : kGuestManagerHeartbeatMissThresholdDefault;
    wdt->guest_manager_watchdog_boot_grace_sec =
        config->boot_grace_sec ? config->boot_grace_sec
                               : kGuestManagerWatchdogBootGraceSecDefault;
    wdt->guest_manager_config_time = now;
    wdt->guest_manager_last_heartbeat = now;
    wdt->guest_manager_heartbeat_count = 0;
    wdt->guest_manager_heartbeat_armed = wdt->guest_manager_watchdog_enabled;
    wdt->guest_manager_heartbeat_lost_logged = false;
    enabled = wdt->guest_manager_watchdog_enabled;
    interval_sec = wdt->guest_manager_heartbeat_interval_sec;
    miss_threshold = wdt->guest_manager_heartbeat_miss_threshold;
    boot_grace_sec = wdt->guest_manager_watchdog_boot_grace_sec;
    mtx_unlock(&wdt->lock);

    zxlogf(INFO, "guest_manager watchdog config: enable=%u interval=%u miss=%u grace=%u\n",
           enabled, interval_sec, miss_threshold, boot_grace_sec);
    return ZX_OK;
}

static int mtk_wdt_set_gm_heartbeat(void* ctx)
{
    mtk_wdt_t* wdt = ctx;
    uint64_t heartbeat_count;
    zx_time_t heartbeat_time;
    uint32_t interval_sec;
    uint32_t miss_threshold;

    if (!wdt) {
        return ZX_ERR_INVALID_ARGS;
    }

    mtx_lock(&wdt->lock);
    if (wdt->supervision.reset_latched) {
        mtx_unlock(&wdt->lock);
        return ZX_ERR_BAD_STATE;
    }
    heartbeat_time = monotonic_now();
    wdt->guest_manager_last_heartbeat = heartbeat_time;
    wdt->guest_manager_heartbeat_count++;
    wdt->guest_manager_heartbeat_armed = true;
    wdt->guest_manager_heartbeat_lost_logged = false;
    heartbeat_count = wdt->guest_manager_heartbeat_count;
    interval_sec = wdt->guest_manager_heartbeat_interval_sec;
    miss_threshold = wdt->guest_manager_heartbeat_miss_threshold;
    mtx_unlock(&wdt->lock);

    if (heartbeat_count == 1 || (heartbeat_count % 6) == 0) {
        zxlogf(SPEW,
               "event=guest_manager_watchdog_heartbeat count=%" PRIu64
               " now_ns=%" PRId64 " interval_sec=%u miss_threshold=%u\n",
               heartbeat_count, heartbeat_time, interval_sec, miss_threshold);
    }
    return ZX_OK;
}

/****************************************/
static int wdt_worker_thread(void* arg)
{
    uint64_t time_out = 0;
    mtk_wdt_t* wdt = (mtk_wdt_t*)arg;
    uint32_t i = 0;
    bool bkick_dog = false;
    uint64_t current_time = 0;
    uint32_t cpu_mask = 0;
    uint32_t cur_cpu;
    zx_handle_t thread_handle = zx_thread_self();
    zx_info_cpu_stats_t stats[8];
    size_t actual, avail;
    const uint64_t one_hundred_ms = zx_ticks_per_second() / 10;

    device_make_visible(wdt->zxdev);
    mtx_lock(&wdt->lock);
    cpu_mask = 1 << wdt->cpu_id;
    zx_thread_set_cpu_affinity(thread_handle, cpu_mask);
    zx_thread_set_priority(kWDTPriority);
    wdt->cpu_id += 1;
    mtx_unlock(&wdt->lock);
    zxlogf(TRACE, "%s:%d, wdt->timeout:%d sencond, cpu_mask:%x\n", __func__,
           __LINE__, wdt->timeout, cpu_mask);
    while (true) {
        bool trigger_hang_detect_reboot = false;
        cur_cpu = zx_thread_get_current_cpu();

        time_out = (wdt->timeout * zx_ticks_per_second()) << 1;
        bkick_dog = true;
        zx_object_get_info(get_root_resource(), ZX_INFO_CPU_STATS, &stats,
                           sizeof(stats), &actual, &avail);
        mtx_lock(&wdt->lock);
        current_time = zx_ticks_get();
        wdt->kick_bits[cur_cpu] = true;
        wdt->last_kick_time[cur_cpu] = current_time;
        if (wdt->bsuspend) {
            mtk_wdt_refresh_timeout_baseline_locked(wdt, current_time, monotonic_now());
        } else {
            for (i = 0; i < MAX_PCPU; i++) {
                if (i != cur_cpu) {
                    if (!wdt->kick_bits[i] ||
                        (CPU_STATS_FLAG_ONLINE != stats[i].flags)) {
                        //wdt-i thread may have not be schedule.
                        wdt->last_kick_time[i] = current_time;
                        zxlogf(SPEW,"CPU %d is wait for schedule! flag:%x.\n", i,
                               stats[i].flags);
                    } else if ((current_time > wdt->last_kick_time[i]) &&
                             (current_time - wdt->last_kick_time[i] > time_out)) {
                        //timeout
                        zxlogf(ERROR,
                               "event=nebula_watchdog_cpu_timeout cpu=%u"
                               " cur_cpu=%u elapsed_ticks=%" PRIu64
                               " timeout_ticks=%" PRIu64 " flag=0x%x\n",
                               i, cur_cpu,
                               current_time - wdt->last_kick_time[i], time_out,
                               stats[i].flags);
                        bkick_dog = false;
                    }
                }
            }
        }

        if (bkick_dog) {
            if (wdt->guest_manager_watchdog_enabled &&
                wdt->guest_manager_heartbeat_armed) {
                const zx_time_t gm_now = monotonic_now();
                const uint64_t boot_grace_ticks =
                    ZX_SEC((int64_t)wdt->guest_manager_watchdog_boot_grace_sec);
                const uint64_t heartbeat_timeout_ticks =
                    ZX_SEC((int64_t)wdt->guest_manager_heartbeat_interval_sec *
                           (int64_t)wdt->guest_manager_heartbeat_miss_threshold);

                if ((gm_now >= wdt->guest_manager_config_time) &&
                    (gm_now - wdt->guest_manager_config_time >
                     boot_grace_ticks) &&
                    (gm_now >= wdt->guest_manager_last_heartbeat) &&
                    (gm_now - wdt->guest_manager_last_heartbeat >
                     heartbeat_timeout_ticks)) {
                    if (!wdt->guest_manager_heartbeat_lost_logged) {
                        zxlogf(ERROR,
                               "event=guest_manager_watchdog_heartbeat_lost"
                               " elapsed_ns=%" PRId64 " timeout_ns=%" PRIu64
                               " heartbeat_count=%" PRIu64
                               " trigger=hang_detect_reboot\n",
                               gm_now -
                                   wdt->guest_manager_last_heartbeat,
                               heartbeat_timeout_ticks,
                               wdt->guest_manager_heartbeat_count);
                        wdt->guest_manager_heartbeat_lost_logged = true;
                    }
                    trigger_hang_detect_reboot =
                        wdt_supervision_fail(&wdt->supervision);
                    bkick_dog = false;
                }
            }
        }

        if (wdt_supervision_check(&wdt->supervision, current_time,
                                  wdt->bsuspend)) {
            zxlogf(ERROR, "event=nebula_watchdog_sos_timeout action=platform_reset\n");
            trigger_hang_detect_reboot = true;
        }
        if (wdt->supervision.reset_latched) {
            bkick_dog = false;
        }
        if (bkick_dog) {
            //nebula all cpu active
            if ((current_time > wdt->nebula_kick_time) &&
                (current_time - wdt->nebula_kick_time < (time_out >> 2))) {
                //have been kicked
            } else {
                //kick onetimes
                //zxlogf(INFO, "%s:%d, nebula kick wdt.\n", __func__, __LINE__);
                if ((current_time > wdt->nebula_kick_time) &&
                    (current_time - wdt->nebula_kick_time > one_hundred_ms)) {
                    wdt->nebula_kick_time = current_time;
                    mtk_wdt_ping(wdt);
                }
            }
        }

        const uint64_t sos_kick = wdt->sos_hartbit_time;
        mtx_unlock(&wdt->lock);
        if (trigger_hang_detect_reboot) {
            exception_reboot(AEE_REBOOT_MODE_WDT);
        }
        if ((current_time > sos_kick) &&
            ((current_time - sos_kick) <
             (TUNE_TIME * zx_ticks_per_second()))) {
            zxlogf(SPEW, "sync with sos kick.\n");
            zx_nanosleep(zx_deadline_after(ZX_SEC(TUNE_TIME -
                         ((current_time - sos_kick) /
                          zx_ticks_per_second())) + ZX_MSEC(500)));
        } else {
            zx_nanosleep(zx_deadline_after(ZX_SEC(wdt->timeout)));
        }
    }

#if 0
     // create a port for receive signal
    zx_handle_t port;
     status = zx_port_create(0, &port);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to create port, status:%d\n", __func__, status);
        return 1;
    }

    // create a timer
    zx_handle_t timer;
    status = zx_timer_create(ZX_CLOCK_MONOTONIC, ZX_CLOCK_MONOTONIC, &timer);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to create timer, status:%d\n", __func__, status);
        zx_handle_close(port);
        return 1;
    }

    //set 2 seconds trigger
    zx_time_t deadline = zx_deadline_after(ZX_SEC(wdt->timeout));
    status = zx_timer_set(timer, deadline, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to set timer, status:%d\n", __func__, status);
        zx_handle_close(timer);
        zx_handle_close(port);
        return 1;
    }

    //wait timer signal
    zx_signals_t signals = ZX_TIMER_SIGNALED;
    status = zx_object_wait_async(timer, port, 0, signals, ZX_WAIT_ASYNC_REPEATING);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to wait asynchronously for timer, status:%d\n", __func__, status);
        zx_handle_close(timer);
        zx_handle_close(port);
        return 1;
    }

    zx_port_packet_t port_packet;
    while (true) {
#if 0
        //Check if the SOS system is functioning properly.
        wdt->wdt_last_updata_time = zx_ticks_get();
        time_out = (wdt->timeout * zx_ticks_per_second()) << 1;
        if (wdt->wdt_last_updata_time - wdt->sos_last_updata_time > time_out) {
            zxlogf(ERROR, "%s:%d, Failed to kick watchdog! sos process might have died!\n", __func__, __LINE__);
            return 1;
        }
#endif
        status = zx_port_wait(port, ZX_TIME_INFINITE, &port_packet, 0);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s: Failed to wait ZX_TIMER_SIGNALED signals on port, status:%d\n", __func__, status);
            break;
        } else if (port_packet.signal.observed == ZX_TIMER_SIGNALED) {
            writel((MTK_WDT_RESTART_KEY), (wdt)->wdt_base + (MTK_WDT_RESTART));
            deadline = zx_deadline_after(ZX_SEC(wdt->timeout));
            zx_timer_set(timer, deadline, 0);
        }
    }

    //clean port and close port
    zx_handle_close(timer);
    zx_handle_close(port);
#endif
    return 0;
}

static zx_status_t wdt_ioctl(void* ctx, uint32_t op, const void* in_buf,
                             size_t in_len, void* out_buf, size_t out_len,
                             size_t* out_actual) {
    //mtk_wdt_t* wdt = ctx;
    switch (op) {
    case IOCTL_GRT_WDT_START:
    case IOCTL_GRT_WDT_STOP: {
        mtk_wdt_t* wdt = ctx;
        mtx_lock(&wdt->lock);
        int result = ZX_ERR_BAD_STATE;
        if (!wdt->supervision.reset_latched &&
            !(op == IOCTL_GRT_WDT_STOP &&
              (wdt->supervision.sos_armed || wdt->guest_manager_watchdog_enabled))) {
            result = op == IOCTL_GRT_WDT_START ? mtk_wdt_start(ctx)
                                              : mtk_wdt_stop(ctx);
        }
        mtx_unlock(&wdt->lock);
        return result;
    }
    case IOCTL_GRT_WDT_PING:
    case IOCTL_GRT_WDT_ACTIVE:
        return mtk_wdt_sos_ping(ctx);
    case IOCTL_GRT_WDT_TRIGGER_RESET:
        return mtk_wdt_request_reset(ctx);
    case IOCTL_GRT_WDT_SET_TIMEOUT:
        if (!in_buf || in_len < sizeof(uint32_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        // Guest interval is independent of the host polling/HW timeout.
        return mtk_wdt_set_sos_timeout(ctx, *(const uint32_t*)in_buf);
    case IOCTL_GRT_SET_RST_WDT:
        mtk_wdt_set_sw_rst_status(ctx);
        return ZX_OK;
    case IOCTL_GRT_SET_SUSPEND_WDT:
        return mtk_wdt_suspend(ctx);
    case IOCTL_GRT_SET_RESUME_WDT:
        return mtk_wdt_resume(ctx);
    case IOCTL_GRT_SET_GM_WATCHDOG_CONFIG:
        if (!in_buf || in_len < sizeof(grt_gm_watchdog_config_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        return mtk_wdt_set_gm_watchdog_config(ctx,
                                              (const grt_gm_watchdog_config_t*)in_buf);
    case IOCTL_GRT_SET_GM_HEARTBEAT:
        return mtk_wdt_set_gm_heartbeat(ctx);

    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static void wdt_unbind(void* ctx) {
    mtk_wdt_t* wdt = ctx;
    // do not deinit wdt, let following system do reinit
    //wdt_deinit(wdt);
    zxlogf(INFO, "%s: %d\n", __func__, __LINE__);
    device_remove(wdt->zxdev);
}

static void wdt_release(void* ctx) {
    mtk_wdt_t* wdt = ctx;
    int i = 0;

    zxlogf(INFO, "%s: %d\n", __func__, __LINE__);
    for (i = 0; i < MAX_PCPU; i++) {
        if (wdt->worker_thread_running[i]) {
            thrd_join(wdt->worker_thread[i], NULL);
        }
    }

    free(wdt);
}

zx_status_t wdt_read(void* ctx, void* buf, size_t count,
                        zx_off_t off, size_t* actual) {
    zxlogf(INFO, "%s: %d\n", __func__, __LINE__);
    return ZX_OK;
}

zx_status_t wdt_write(void* ctx, const void* buf, size_t count,
                         zx_off_t off, size_t* actual) {
    zxlogf(INFO, "%s: %d\n", __func__, __LINE__);
    return ZX_OK;
}

static zx_protocol_device_t wdt_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = wdt_ioctl,
    .unbind = wdt_unbind,
    .release = wdt_release,
    .read = wdt_read,
    .write = wdt_write,
};

static zx_status_t wdt_protocol_get_info(void* ctx, zx_handle_t* handle_out) {
    zxlogf(INFO, "%s: %d\n", __func__, __LINE__);
    return ZX_OK;
}

static zx_status_t create_wdt_thread_bind_cpus(void *ctx) {
    zx_status_t status = ZX_OK;
    mtk_wdt_t* wdt = (mtk_wdt_t *)ctx;
    uint i = 0;

    zxlogf(TRACE, "%s: %d\n", __func__, __LINE__);
    //create pcpu wdt threads
    for (i = 0; i < MAX_PCPU; i++) {
        char name[16];
        snprintf(name, sizeof(name), "wdt-%u", i);
        //wdt->cpu_id = i;
        wdt->kick_bits[i] = false;
        wdt->last_kick_time[i] = zx_ticks_get();

        status = thrd_create_with_name(&wdt->worker_thread[i],
                                       wdt_worker_thread, wdt, name);
        if (status != thrd_success) {
            status = thrd_status_to_zx_status(status);
            zxlogf(ERROR, "%s: %d,status:%x\n", __func__, __LINE__,status);
            return status;
        } else {
            thrd_detach(wdt->worker_thread[i]);
            wdt->worker_thread_running[i] = true;
        }
    }

    zxlogf(INFO, "%u wdt threads created.\n", MAX_PCPU);

    return status;
}

static zx_status_t wdt_bind(void* ctx, zx_device_t* parent) {
    uint32_t log_flags = ZX_LOG_ERROR | ZX_LOG_WARN | ZX_LOG_INFO;
#if ZX_DEBUGLEVEL
    log_flags |= ZX_LOG_TRACE;
#endif
    driver_set_log_flags(log_flags);

    mtk_wdt_t* wdt = calloc(1, sizeof(mtk_wdt_t));
    if (!wdt) {
        return ZX_ERR_NO_MEMORY;
    }
    zxlogf(TRACE, "%s: %d\n", __func__, __LINE__);
    if (!enable_mtk_wdt)
        return ZX_OK;

    zx_status_t status = ZX_OK;
    status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV,
                                 (void*)&wdt->pdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PLATFORM_DEV not available, status:%x\n",
                __func__, status);
        goto fail;
    }
    status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS,
                                 (void*)&wdt->pbus);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PLATFORM_BUS not available\n",
                __func__);
        goto fail;
    }

    status = pdev_get_device_info(&wdt->pdev, &wdt->dev_info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_get_device_info failed\n", __func__);
        goto fail;
    }

    size_t size;
    status = pdev_map_mmio(&wdt->pdev, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                           &wdt->wdt_base, &size, &wdt->vmo_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_map_mmio_buffer failed\n", __func__);
        goto fail;
    }

    // Create the device.
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "mtk_wdt",
        .ctx = wdt,
        .ops = &wdt_device_proto,
        .flags = DEVICE_ADD_INVISIBLE | DEVICE_ADD_MUST_ISOLATE,
    };
    status = device_add(parent, &args, &wdt->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: %d\n", __func__, __LINE__);
        goto fail;
    }

    mtx_init(&wdt->lock, mtx_plain);
    wdt->timeout = DEF_TIMEOUT;
    wdt->wdt_last_updata_time = wdt->sos_hartbit_time = zx_ticks_get();
    wdt->supervision.sos_timeout = 2ULL * DEF_TIMEOUT * zx_ticks_per_second();
    wdt->guest_manager_heartbeat_interval_sec =
        kGuestManagerHeartbeatIntervalSecDefault;
    wdt->guest_manager_heartbeat_miss_threshold =
        kGuestManagerHeartbeatMissThresholdDefault;
    wdt->guest_manager_watchdog_boot_grace_sec =
        kGuestManagerWatchdogBootGraceSecDefault;
    mtk_wdt_set_timeout(wdt, DEF_TIMEOUT);

    status = create_wdt_thread_bind_cpus(wdt);
    if (status != thrd_success) {
        status = thrd_status_to_zx_status(status);
        zxlogf(ERROR, "%s: %d\n", __func__, __LINE__);
        goto fail_remove;
    }

    return ZX_OK;

fail_remove:
    device_remove(wdt->zxdev);
fail:
    if (wdt) {
        free(wdt);
    }
    return status;
}

static zx_driver_ops_t wdt_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = wdt_bind,

};

ZIRCON_DRIVER_BEGIN(wdt, wdt_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MTK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_GRT_WDT),
ZIRCON_DRIVER_END(wdt)
