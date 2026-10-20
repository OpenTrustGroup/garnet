// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2024 The GoldenRiverTek Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <zircon/device/rtc.h>
#include <zircon/threads.h>
#include <zircon/vmid.h>
#include <hw/reg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/eint.h>
#include <ddk/protocol/gpio.h>

#include "rtc.h"
#include "registers.h"
#include "smc_pmic_simple.h"

// #define RTC_ALARM_SIM_MODE
#define SPMI_HW_WITH_RTC

#define MAX_VM_COUNT            4
#define RTC_ALARM_IRQ           215
#define SPMI_MAX_SLAVE_ID       16
#define SPMI_RTC_IRQ_ID         9
#define YOCTO_VMID              ZX_GUEST_VMID_SOS
#define ANDROID_VMID            ZX_GUEST_VMID_ALPS
#define TBOX_VMID               ZX_GUEST_VMID_TBOX

typedef struct {
    rtc_t tm;
    bool wake_os_flag;
    bool poweroff_alarm_flag;
    unsigned char alarm_enabled;
    unsigned char alarm_pending;
    zx_handle_t event_handle;
} os_rtc_info_t;

static os_rtc_info_t rtc_array[MAX_VM_COUNT];
static unsigned int min_index = MAX_VM_COUNT;
static bool alarm_from_vms_flag = false;
static mtx_t rtc_array_lock;

struct mtk_spmi {
    void *spmimst_base_mmio[3];
    size_t spmimst_mmio_size[3];
    zx_handle_t spmimst_mmio_vmo[3];
    uint32_t *spmimst_regs;
};

typedef struct {
    zx_device_t* zxdev;
    platform_device_protocol_t pdev;
    platform_bus_protocol_t pbus;
    pdev_device_info_t dev_info;
    eint_protocol_t eint_p;
    uint32_t irq;
    mtx_t lock;
    mtx_t clk_lock;
    int mclk_counter;
    uint32_t addr_base;
    smc_pmic_client_t pmic;
    uint32_t wrtgr;
	uint32_t hwid;
	uint32_t chip_version;

    thrd_t work_thread;
    zx_handle_t work_event;
    volatile bool work_stop;
    enum {
        RTC_WAKEUP_IDLE = 0,
        RTC_WAKEUP_IRQ_HELD,
        RTC_WAKEUP_WORK_QUEUED,
    } wakeup_state;
    zx_handle_t handle;
    struct mtk_spmi spmi;

#ifdef RTC_ALARM_SIM_MODE
	bool alarm_sim_mode;
	thrd_t alarm_sim_thread;
	zx_handle_t alarm_sim_stop;
	rtc_t alarm_sim_target;
	uint32_t alarm_sim_vmid;
	mtx_t alarm_sim_lock;
#endif

#ifdef __IRQ_DEBUG__
    uint64_t irq_count;
#endif
} rtc_device_t;

typedef struct {
    rtc_device_t* rtc;
    zx_device_t* device;
} rtc_client_device_t;

static zx_status_t mtk_rtc_read_time(rtc_device_t* rtc, rtc_t* tm);

static zx_status_t rtc_bulk_read(rtc_device_t* rtc, unsigned int reg,
                                  uint16_t* val, size_t val_count) {
    zx_status_t ret;
    ret = smc_pmic_bulk_read(&rtc->pmic, reg, val, val_count);
    return ret;
}

static zx_status_t rtc_bulk_write(rtc_device_t* rtc, unsigned int reg,
                                   const uint16_t* val, size_t val_count) {
    zx_status_t ret;
    ret = smc_pmic_bulk_write(&rtc->pmic, reg, val, val_count);
    return ret;
}

static zx_status_t rtc_write(rtc_device_t* rtc, unsigned int reg, unsigned int val) {
    zx_status_t ret = smc_pmic_bulk_write(&rtc->pmic, reg, &val, 1);
    return ret;
}

static zx_status_t rtc_read(rtc_device_t* rtc, unsigned int reg, unsigned int* val) {
    zx_status_t ret = smc_pmic_bulk_read(&rtc->pmic, reg, val, 1);
    return ret;
}

static zx_status_t regmap_read(rtc_device_t* rtc, unsigned int reg, unsigned int* val) {
    zx_status_t ret = smc_pmic_read_reg(&rtc->pmic, reg, val);
    return ret;
}

static zx_status_t regmap_write(rtc_device_t* rtc, unsigned int reg, unsigned int val) {
    zx_status_t ret = smc_pmic_write_reg(&rtc->pmic, reg, val);
    return ret;
}

static int rtc_update_bits(rtc_device_t *rtc, unsigned int reg,
					  unsigned int mask, unsigned int val) {
    int ret;
	unsigned int tmp, orig = 0;

	ret = rtc_read(rtc, reg, &orig);
	if (ret != 0)
		return ret;
	tmp = orig & ~mask;
	tmp |= val & mask;

	ret = rtc_write(rtc, reg, tmp);

	return ret;
}

static void power_on_mclk(rtc_device_t* rtc) {
    mtx_lock(&rtc->clk_lock);

    /* Power on RTC MCLK and 32k clk before write RTC register */
    regmap_write(rtc, RG_RTC_32K_CK_PDN_CLR, RG_RTC_32K_CK_PDN_MASK);
    regmap_write(rtc, RG_RTC_MCLK_PDN_CLR, RG_RTC_MCLK_PDN_MASK);

    rtc->mclk_counter++;
    zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));

    mtx_unlock(&rtc->clk_lock);
}

static void power_down_mclk(rtc_device_t* rtc) {
    mtx_lock(&rtc->clk_lock);

    rtc->mclk_counter--;
    if (rtc->mclk_counter < 0) {
        zxlogf(ERROR, "rtc: mclk counter underflow!\n");
        rtc->mclk_counter = 0;
    }

    if (rtc->mclk_counter == 0) {
        /* Power down RTC MCLK and 32k clk after write RTC register */
        regmap_write(rtc,  MT6685_SCK_TOP_CKPDN_CON0_L_SET, RG_RTC_32K_CK_PDN_MASK);
        regmap_write(rtc,  MT6685_SCK_TOP_CKPDN_CON0_L_SET, RG_RTC_MCLK_PDN_MASK);
        zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
    }

    mtx_unlock(&rtc->clk_lock);
}

static zx_status_t mtk_rtc_write_trigger(rtc_device_t* rtc) {
    zx_status_t status = ZX_OK;
    uint32_t data;
    int retry = 0;
    const int max_retry = 100000;

    rtc_write(rtc, rtc->addr_base + rtc->wrtgr, 1);

    while (retry < max_retry) {
        status = rtc_read(rtc, rtc->addr_base + RTC_BBPU, &data);
        if (status < 0)
			break;
        if (!(data & RTC_BBPU_CBUSY))
            break;
        zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
        retry++;
    }

    if (retry >= max_retry || (data & RTC_BBPU_CBUSY)) {
        zxlogf(ERROR, "rtc: write trigger timeout\n");
        status = ZX_ERR_TIMED_OUT;
    }

    return status;
}

static int64_t rtc_time_diff(const rtc_t* a, const rtc_t* b) {
    struct tm ta = {
        .tm_sec = a->seconds,
        .tm_min = a->minutes,
        .tm_hour = a->hours,
        .tm_mday = a->day,
        .tm_mon = a->month,
        .tm_year = a->year,
    };
    struct tm tb = {
        .tm_sec = b->seconds,
        .tm_min = b->minutes,
        .tm_hour = b->hours,
        .tm_mday = b->day,
        .tm_mon = b->month,
        .tm_year = b->year,
    };
    time_t sa = mktime(&ta);
    time_t sb = mktime(&tb);
    return (int64_t)difftime(sb, sa);
}

static bool mtk_rtc_check_set_time(rtc_device_t *rtc, rtc_t *tm,
	int retry_time, int rtc_time_reg)
{
	int ret, i, j, write_fail = 0, prot_key = 0xDEAD, hwid = 0, mclk = 0;
	uint16_t data[RTC_OFFSET_COUNT], latest[RTC_OFFSET_COUNT];

	data[RTC_OFFSET_SEC] = tm->seconds;
	data[RTC_OFFSET_MIN] = tm->minutes;
	data[RTC_OFFSET_HOUR] = tm->hours;
	data[RTC_OFFSET_DOM] = tm->day;
	data[RTC_OFFSET_MTH] = tm->month;
	data[RTC_OFFSET_YEAR] = tm->year;

	for (j = 1; j <= retry_time; j++) {

		write_fail = 0;

		ret = rtc_bulk_read(rtc, rtc->addr_base + rtc_time_reg,
				       latest, RTC_OFFSET_COUNT);
		if (ret < 0)
			return false;

		for (i = 0; i < RTC_OFFSET_COUNT; i++) {
			if (i == RTC_OFFSET_DOW)
				continue;

			latest[i] = latest[i] & rtc_time_mask[i];
			if (latest[i] != data[i])
				write_fail++;

			if (j == retry_time) {
				ret = rtc_read(rtc, HWID_MT6685, &hwid);
				if (ret < 0)
					return false;

				ret = rtc_read(rtc, RG_RTC_MCLK_PDN, &mclk);
				if (ret < 0)
					return false;
				mclk = mclk >> RG_RTC_MCLK_PDN_STA_SHIFT & RG_RTC_MCLK_PDN_STA_MASK;

				if (rtc->chip_version == MT6685_SERIES ||
					rtc->chip_version == MT6687_SERIES) {
					ret = rtc_read(rtc,
						rtc->addr_base + RTC_SPAR_MACRO, &prot_key);

					if (ret < 0)
						return false;

					prot_key =
						prot_key >> SPAR_PROT_STAT_SHIFT
						& SPAR_PROT_STAT_MASK;
				}

				zxlogf(INFO, "[HWID 0x%x, MCLK 0x%x, prot key 0x%x] %s write %d, latest %d\n",
				    hwid, mclk, prot_key, rtc_time_reg_name[i], data[i], latest[i]);
			}
		}

		if (write_fail > 0)
			zx_nanosleep(zx_deadline_after(ZX_MSEC(2)));
		else
			break;
	}

	if (write_fail > 0)
		return false;

	return true;
}


static int64_t rtc_diff_seconds(const rtc_t* now, const rtc_t* target) {
	struct tm tm_now = {
		.tm_sec = now->seconds,
		.tm_min = now->minutes,
		.tm_hour = now->hours,
		.tm_mday = now->day,
		.tm_mon = now->month - 1,
		.tm_year = now->year + RTC_BASE_YEAR - 1900,
	};
	struct tm tm_target = {
		.tm_sec = target->seconds,
		.tm_min = target->minutes,
		.tm_hour = target->hours,
		.tm_mday = target->day,
		.tm_mon = target->month - 1,
		.tm_year = target->year + RTC_BASE_YEAR - 1900,
	};

	time_t t_now = mktime(&tm_now);
	time_t t_target = mktime(&tm_target);

	return (int64_t)difftime(t_target, t_now);
}

#ifdef RTC_ALARM_SIM_MODE

static int alarm_sim_thread_func(void* arg) {
	rtc_device_t* rtc = (rtc_device_t*)arg;

	zxlogf(INFO, "rtc: alarm simulation thread started\n");

	while (true) {
		mtx_lock(&rtc->alarm_sim_lock);
		bool has_valid_alarm = (rtc->alarm_sim_target.year != 0 ||
							   rtc->alarm_sim_target.month != 0 ||
							   rtc->alarm_sim_target.day != 0 ||
							   rtc->alarm_sim_target.hours != 0 ||
							   rtc->alarm_sim_target.minutes != 0 ||
							   rtc->alarm_sim_target.seconds != 0);

		if (!has_valid_alarm) {
			mtx_unlock(&rtc->alarm_sim_lock);
			zx_signals_t observed;
			zx_status_t status = zx_object_wait_one(rtc->alarm_sim_stop,
													ZX_USER_SIGNAL_0,
													ZX_TIME_INFINITE,
													&observed);
			if (status == ZX_OK && (observed & ZX_USER_SIGNAL_0)) {
				zx_object_signal(rtc->alarm_sim_stop, ZX_USER_SIGNAL_0, 0);
				mtx_lock(&rtc->alarm_sim_lock);
				bool should_exit = (rtc->alarm_sim_stop == ZX_HANDLE_INVALID);
				mtx_unlock(&rtc->alarm_sim_lock);
				if (should_exit) {
					zxlogf(INFO, "rtc: alarm simulation thread exiting (handle closed)\n");
					break;
				}
				continue;
			}
			break;
		}

		rtc_t now;
		if (mtk_rtc_read_time(rtc, &now) != ZX_OK) {
			mtx_unlock(&rtc->alarm_sim_lock);
			zxlogf(ERROR, "rtc: failed to read time in alarm sim thread\n");
			break;
		}

		int64_t diff = rtc_diff_seconds(&now, &rtc->alarm_sim_target);
		mtx_unlock(&rtc->alarm_sim_lock);

		if (diff <= 0) {
			zxlogf(INFO, "rtc: simulated alarm triggered for VM[%u]!\n", rtc->alarm_sim_vmid);

			mtx_lock(&rtc_array_lock);
			if (rtc->alarm_sim_vmid < MAX_VM_COUNT) {
				if (rtc_array[rtc->alarm_sim_vmid].event_handle != ZX_HANDLE_INVALID) {
					zx_object_signal(rtc_array[rtc->alarm_sim_vmid].event_handle, 0, ZX_USER_SIGNAL_0);
					zxlogf(INFO, "rtc: simulated alarm signaled event for VM[%u]\n", rtc->alarm_sim_vmid);
				} else {
					zxlogf(ERROR, "rtc: event handle for VM[%u] is INVALID!\n", rtc->alarm_sim_vmid);
				}
			} else {
				zxlogf(ERROR, "rtc: alarm_sim_vmid %u >= MAX_VM_COUNT %d\n", rtc->alarm_sim_vmid, MAX_VM_COUNT);
			}
			mtx_unlock(&rtc_array_lock);

			mtx_lock(&rtc->alarm_sim_lock);
			memset(&rtc->alarm_sim_target, 0, sizeof(rtc->alarm_sim_target));
			mtx_unlock(&rtc->alarm_sim_lock);

			zx_signals_t observed;
			zx_status_t status = zx_object_wait_one(rtc->alarm_sim_stop,
													ZX_USER_SIGNAL_0,
													zx_deadline_after(ZX_SEC(1)),
													&observed);
			if (status == ZX_OK && (observed & ZX_USER_SIGNAL_0)) {
				zx_object_signal(rtc->alarm_sim_stop, ZX_USER_SIGNAL_0, 0);

				mtx_lock(&rtc->alarm_sim_lock);
				bool should_exit = (rtc->alarm_sim_stop == ZX_HANDLE_INVALID);
				mtx_unlock(&rtc->alarm_sim_lock);
				if (should_exit)
					break;
			}
			continue;
		}

		int64_t wait_seconds = (diff > 60) ? 60 : diff;
		zx_time_t deadline = zx_deadline_after(ZX_SEC(wait_seconds));

		zx_signals_t observed;
		zx_status_t status = zx_object_wait_one(rtc->alarm_sim_stop,
												ZX_USER_SIGNAL_0,
												deadline,
												&observed);
		if (status == ZX_OK && (observed & ZX_USER_SIGNAL_0)) {
			zx_object_signal(rtc->alarm_sim_stop, ZX_USER_SIGNAL_0, 0);
			mtx_lock(&rtc->alarm_sim_lock);
			bool should_exit = (rtc->alarm_sim_stop == ZX_HANDLE_INVALID);
			mtx_unlock(&rtc->alarm_sim_lock);
			if (should_exit) {
				zxlogf(INFO, "rtc: alarm simulation thread exiting (handle closed)\n");
				break;
			}
		}
	}

	zxlogf(INFO, "rtc: alarm simulation thread exiting\n");
	return 0;
}


static zx_status_t start_alarm_sim(rtc_device_t* rtc, uint32_t vmid, const rtc_t* target) {
	mtx_lock(&rtc->alarm_sim_lock);

	bool thread_already_running = (rtc->alarm_sim_thread != 0);

	rtc->alarm_sim_target = *target;
	rtc->alarm_sim_vmid = vmid;

	if (thread_already_running && rtc->alarm_sim_stop != ZX_HANDLE_INVALID) {
		zx_object_signal(rtc->alarm_sim_stop, 0, ZX_USER_SIGNAL_0);
		mtx_unlock(&rtc->alarm_sim_lock);
		zxlogf(INFO, "rtc: alarm simulation updated for VM[%u] at %04d-%02d-%02d %02d:%02d:%02d\n",
			vmid,
			target->year + RTC_BASE_YEAR + RTC_MIN_YEAR_OFFSET, target->month,
			target->day, target->hours, target->minutes, target->seconds);
		return ZX_OK;
	}

	if (rtc->alarm_sim_stop == ZX_HANDLE_INVALID) {
		zx_status_t status = zx_event_create(0, &rtc->alarm_sim_stop);
		if (status != ZX_OK) {
			zxlogf(ERROR, "rtc: failed to create alarm sim stop event: %d\n", status);
			mtx_unlock(&rtc->alarm_sim_lock);
			return status;
		}
	}

	mtx_unlock(&rtc->alarm_sim_lock);

	int ret = thrd_create_with_name(&rtc->alarm_sim_thread,
									alarm_sim_thread_func,
									rtc,
									"rtc-alarm-sim");
	if (ret != thrd_success) {
		zxlogf(ERROR, "rtc: failed to create alarm simulation thread: %d\n", ret);
		return ZX_ERR_NO_RESOURCES;
	}

	zxlogf(INFO, "rtc: alarm simulation started for VM[%u] at %04d-%02d-%02d %02d:%02d:%02d\n",
		vmid,
		target->year + RTC_BASE_YEAR + RTC_MIN_YEAR_OFFSET, target->month,
		target->day, target->hours, target->minutes, target->seconds);

	return ZX_OK;
}

static void stop_alarm_sim(rtc_device_t* rtc) {
	mtx_lock(&rtc->alarm_sim_lock);

	memset(&rtc->alarm_sim_target, 0, sizeof(rtc->alarm_sim_target));

	if (rtc->alarm_sim_stop != ZX_HANDLE_INVALID) {
		zx_object_signal(rtc->alarm_sim_stop, 0, ZX_USER_SIGNAL_0);
	}

	mtx_unlock(&rtc->alarm_sim_lock);
}

#endif  // RTC_ALARM_SIM_MODE

static zx_status_t init_vm_event_handle(int32_t vmid) {
	if (vmid < 0 || vmid >= MAX_VM_COUNT) {
		zxlogf(ERROR, "rtc: invalid VM index %d for event init\n", vmid);
		return ZX_ERR_INVALID_ARGS;
	}

	mtx_lock(&rtc_array_lock);

	if (rtc_array[vmid].event_handle != ZX_HANDLE_INVALID) {
		zxlogf(INFO, "rtc: event handle for VM[%d] already initialized\n", vmid);
		mtx_unlock(&rtc_array_lock);
		return ZX_OK;
	}

	zx_status_t status = zx_event_create(0, &rtc_array[vmid].event_handle);
	if (status != ZX_OK) {
		zxlogf(ERROR, "rtc: failed to create event for VM[%d]: %d\n", vmid, status);
		mtx_unlock(&rtc_array_lock);
		return status;
	}

	zx_signals_t observed = 0;
	status = zx_object_wait_one(rtc_array[vmid].event_handle, 0, 0, &observed);
	if (status != ZX_OK && status != ZX_ERR_TIMED_OUT) {
		zxlogf(ERROR, "rtc: created event handle for VM[%d] is invalid: %d\n", vmid, status);
		zx_handle_close(rtc_array[vmid].event_handle);
		rtc_array[vmid].event_handle = ZX_HANDLE_INVALID;
		mtx_unlock(&rtc_array_lock);
		return status;
	}

	zxlogf(INFO, "rtc: initialized and verified event handle for VM[%d], event handle:%d\n", vmid, rtc_array[vmid].event_handle);
	mtx_unlock(&rtc_array_lock);
	return ZX_OK;
}

static zx_status_t mtk_rtc_set_alarm(rtc_device_t* rtc, const os_rtc_info_t* rtc_info) {
    uint16_t data[RTC_OFFSET_COUNT];
    zx_status_t status;
    rtc_t *alarm = &rtc_info->tm;

    zxlogf(INFO, "rtc: set alarm %04d-%02d-%02d %02d:%02d:%02d, alarm enbale:%d\n",
           alarm->year, alarm->month, alarm->day,
           alarm->hours, alarm->minutes, alarm->seconds, rtc_info->alarm_enabled);

    power_on_mclk(rtc);

    mtx_lock(&rtc->lock);
    if (rtc_info->alarm_enabled) {
        status = rtc_update_bits(rtc,
                rtc->addr_base + RTC_PDN2, RTC_PDN2_PWRON_ALARM, 0);
        if (status != ZX_OK)
            goto exit;
        mtk_rtc_write_trigger(rtc);

        data[RTC_OFFSET_SEC]  = ((data[RTC_OFFSET_SEC] & ~(RTC_AL_SEC_MASK)) | (alarm->seconds & RTC_AL_SEC_MASK));
        data[RTC_OFFSET_MIN]  = ((data[RTC_OFFSET_MIN] & ~(RTC_AL_MIN_MASK)) | (alarm->minutes & RTC_AL_MIN_MASK));
        data[RTC_OFFSET_HOUR] = ((data[RTC_OFFSET_HOUR] & ~(RTC_AL_HOU_MASK)) | (alarm->hours & RTC_AL_HOU_MASK));
        data[RTC_OFFSET_DOM]  = ((data[RTC_OFFSET_DOM] & ~(RTC_AL_DOM_MASK)) | (alarm->day & RTC_AL_DOM_MASK));
        data[RTC_OFFSET_MTH]  = ((data[RTC_OFFSET_MTH] & ~(RTC_AL_MTH_MASK)) | (alarm->month & RTC_AL_MTH_MASK));
        data[RTC_OFFSET_YEAR] = ((data[RTC_OFFSET_YEAR] & ~(RTC_AL_YEA_MASK)) | (alarm->year & RTC_AL_YEA_MASK));

        status = rtc_bulk_write(rtc, rtc->addr_base + RTC_AL_SEC, data, RTC_OFFSET_COUNT);
        if (status != ZX_OK)
            goto exit;

        status = rtc_write(rtc, rtc->addr_base + RTC_AL_MASK, RTC_AL_MASK_DOW);
        if (status != ZX_OK)
                goto exit;

        status = rtc_update_bits(rtc, rtc->addr_base + RTC_IRQ_EN,
                                RTC_IRQ_EN_AL, RTC_IRQ_EN_AL);
        if (status != ZX_OK)
            goto exit;
    } else {
        status = rtc_update_bits(rtc, rtc->addr_base + RTC_IRQ_EN,
							RTC_IRQ_EN_AL, 0);
        if (status != ZX_OK)
            goto exit;
    }

    mtk_rtc_write_trigger(rtc);

    if (rtc_info->alarm_enabled) {
        if (!mtk_rtc_check_set_time(rtc, alarm, 2, RTC_AL_SEC)) {
            zxlogf(ERROR, "check set alarm failed.\n");
            status = ZX_ERR_BAD_STATE;
        }
    }

exit:
    mtx_unlock(&rtc->lock);
    power_down_mclk(rtc);
    return status;
}

static void rtc_update_next_alarm(rtc_device_t* rtc) {
    rtc_t now;
    unsigned int index, next_min_index = MAX_VM_COUNT;
    int64_t min_diff = INT64_MAX;

    if (mtk_rtc_read_time(rtc, &now) != ZX_OK) {
        zxlogf(ERROR, "rtc: failed to read current time\n");
        return;
    }

    // mtx_lock(&rtc_array_lock);

    for (index = 0; index < MAX_VM_COUNT; index++) {
        if (rtc_array[index].tm.year == 0 && rtc_array[index].tm.month == 0 &&
            rtc_array[index].tm.day == 0 && rtc_array[index].tm.hours == 0 &&
            rtc_array[index].tm.minutes == 0 && rtc_array[index].tm.seconds == 0) {
            continue;
        }

        int64_t diff = rtc_time_diff(&now, &rtc_array[index].tm);
        zxlogf(INFO, "rtc: diff time:%lld\n", diff);

        if (diff > 0 && diff < min_diff) {
            min_diff = diff;
            next_min_index = index;
        }
    }
    zxlogf(INFO, "rtc: next_min_index:%lld\n", next_min_index);

    if (next_min_index != MAX_VM_COUNT) {
        for (index = 0; index < MAX_VM_COUNT; index++) {
            rtc_array[index].wake_os_flag = false;
        }

        // Set wake_os_flag for ALL VMs that have the same alarm time as the next alarm
        // This ensures all VMs with the same alarm time are notified when it triggers
        for (index = 0; index < MAX_VM_COUNT; index++) {
            if (rtc_array[index].tm.year == 0 && rtc_array[index].tm.month == 0 &&
                rtc_array[index].tm.day == 0 && rtc_array[index].tm.hours == 0 &&
                rtc_array[index].tm.minutes == 0 && rtc_array[index].tm.seconds == 0) {
                continue;  // Skip unset alarms
            }

            // Check if this VM has the same alarm time as the next minimum
            int64_t diff_to_next = rtc_time_diff(&rtc_array[next_min_index].tm, &rtc_array[index].tm);
            if (diff_to_next == 0) {
                rtc_array[index].wake_os_flag = true;
                zxlogf(INFO, "rtc: VM[%d] will be notified at next alarm\n", index);
            }
        }

        min_index = next_min_index;

        zxlogf(INFO, "rtc: next alarm is VM[%d] at %04d-%02d-%02d %02d:%02d:%02d\n",
               min_index,
               rtc_array[min_index].tm.year + RTC_BASE_YEAR + RTC_MIN_YEAR_OFFSET, rtc_array[min_index].tm.month,
               rtc_array[min_index].tm.day, rtc_array[min_index].tm.hours,
               rtc_array[min_index].tm.minutes, rtc_array[min_index].tm.seconds);

        if (mtk_rtc_set_alarm(rtc, &rtc_array[min_index]) != ZX_OK) {
            zxlogf(ERROR, "rtc: set alarm failed.");
        }

    }
    // mtx_unlock(&rtc_array_lock);
}

static void mtk_rtc_reset_bbpu_alarm_status(rtc_device_t* rtc) {
    uint32_t bbpu = 0;

    power_on_mclk(rtc);

    bbpu = RTC_BBPU_KEY | RTC_BBPU_PWREN | RTC_BBPU_RESET_AL;
    rtc_write(rtc, rtc->addr_base + RTC_BBPU, bbpu);
    mtk_rtc_write_trigger(rtc);

    power_down_mclk(rtc);
}

static int mtk_rtc_is_alarm_irq(rtc_device_t *rtc)
{
	uint32_t irqsta = 0, bbpu = 0, sck = 0, sck_check = 0, irqsta_check = 0;
	int ret;

	power_on_mclk(rtc);

	ret = rtc_read(rtc, rtc->addr_base + RTC_IRQ_STA, &irqsta);/* read clear */

	/*clear SCK_TOP rtc interrupt*/
	rtc_read(rtc, SCK_TOP_INT_STATUS0, &sck);
	rtc_write(rtc, SCK_TOP_INT_STATUS0, sck);

	rtc_read(rtc, SCK_TOP_INT_STATUS0, &sck_check);
	if (sck_check) {
		zx_nanosleep(zx_deadline_after(ZX_USEC(70)));
		rtc_write(rtc, SCK_TOP_INT_STATUS0, 1);

		rtc_read(rtc, SCK_TOP_INT_STATUS0, &sck_check);
		if (sck_check) {
			zxlogf(INFO, "%s: TOP INT STA 0x%x\n", __func__, sck_check);
			rtc_read(rtc, rtc->addr_base + RTC_IRQ_STA, &irqsta_check);
			zxlogf(INFO, "%s: IRQ STA 0x%x\n", __func__, irqsta_check);
		}
	}

	if ((ret == 0) && (irqsta & RTC_IRQ_STA_AL)) {
		bbpu = RTC_BBPU_KEY | RTC_BBPU_PWREN;
		ret = rtc_write(rtc,
					rtc->addr_base + RTC_BBPU, bbpu);
		if (ret < 0)
			zxlogf(ERROR, "%s: %d error\n", __func__, __LINE__);

		ret =  rtc_update_bits(rtc,
				rtc->addr_base + RTC_IRQ_EN,
				RTC_IRQ_EN_AL, 0);
		if (ret < 0)
			zxlogf(ERROR, "%s: %d error\n", __func__, __LINE__);
		mtk_rtc_write_trigger(rtc);
		power_down_mclk(rtc);
		return RTC_ALSTA;
	}
	power_down_mclk(rtc);
	return RTC_NONE;
}

static void rtc_alarm_handle(rtc_device_t* rtc) {
    unsigned int index;
    bool found_alarm = false;

    mtx_lock(&rtc_array_lock);

    // Collect all VMs with wake_os_flag set
    unsigned int triggered_vms[MAX_VM_COUNT];
    unsigned int triggered_count = 0;

    for (index = 0; index < MAX_VM_COUNT; index++) {
        if (rtc_array[index].wake_os_flag) {
            triggered_vms[triggered_count++] = index;
            found_alarm = true;
        }
    }

    if (found_alarm) {
        for (unsigned int i = 0; i < triggered_count; i++) {
            index = triggered_vms[i];

            if (rtc_array[index].event_handle == ZX_HANDLE_INVALID) {
                mtx_unlock(&rtc_array_lock);
                zx_status_t init_status = init_vm_event_handle(index);
                if (init_status != ZX_OK) {
                    zxlogf(ERROR, "rtc: failed to initialize VM[%d] event handle in alarm handler\n", index);
                    // Continue with other VMs even if one fails
                    mtx_lock(&rtc_array_lock);
                    continue;
                }
            }
        }

        // Second pass: clear flags and signal all VMs
        for (unsigned int i = 0; i < triggered_count; i++) {
            index = triggered_vms[i];

            rtc_array[index].wake_os_flag = false;
            rtc_array[index].tm.year = 0;
            rtc_array[index].tm.month = 0;
            rtc_array[index].tm.day = 0;
            rtc_array[index].tm.hours = 0;
            rtc_array[index].tm.minutes = 0;
            rtc_array[index].tm.seconds = 0;
            rtc_array[index].alarm_enabled = 0;

            zxlogf(INFO, "rtc: VM[%d] alarm triggered\n", index);

            if (rtc_array[index].event_handle != ZX_HANDLE_INVALID) {
                zx_object_signal(rtc_array[index].event_handle, 0, ZX_USER_SIGNAL_0);
                zxlogf(INFO, "rtc: triggered event for VM[%d]\n", index);
            } else {
                zxlogf(ERROR, "rtc: event handle for VM[%d] is invalid\n", index);
            }
        }

        // Update next alarm for all VMs
        rtc_update_next_alarm(rtc);
    }

    mtx_unlock(&rtc_array_lock);
}

static int rtc_work_thread(void* arg) {
    rtc_device_t* rtc = (rtc_device_t*)arg;
    zx_status_t status;
    bool should_unlock;

    while (!rtc->work_stop) {
        status = zx_object_wait_one(rtc->work_event, ZX_USER_SIGNAL_0, ZX_TIME_INFINITE, NULL);
        if (status != ZX_OK) {
            if (status == ZX_ERR_CANCELED) {
                break;  // Event was destroyed
            }
            zxlogf(ERROR, "rtc: work thread wait failed: %d\n", status);
            continue;
        }

        zx_object_signal(rtc->work_event, ZX_USER_SIGNAL_0, 0);

        if (rtc->work_stop) {
            mtx_lock(&rtc->lock);
            should_unlock = (rtc->wakeup_state != RTC_WAKEUP_IDLE);
            rtc->wakeup_state = RTC_WAKEUP_IDLE;
            mtx_unlock(&rtc->lock);
            if (should_unlock)
                zx_wakeup_source_unlock(rtc->handle);
            break;
        }

        rtc_alarm_handle(rtc);
        mtx_lock(&rtc->lock);
        should_unlock = (rtc->wakeup_state != RTC_WAKEUP_IDLE);
        rtc->wakeup_state = RTC_WAKEUP_IDLE;
        mtx_unlock(&rtc->lock);
        if (should_unlock)
            zx_wakeup_source_unlock(rtc->handle);
    }

    zxlogf(INFO, "rtc: work thread exiting\n");
    return 0;
}

static inline uint32_t read_reg(volatile void *base, uint32_t offset) {
  return *(volatile uint32_t *)((uint8_t *)base + offset);
}

static inline void write_reg(volatile void *base, uint32_t offset,
                             uint32_t value) {
  *(volatile uint32_t *)((uint8_t *)base + offset) = value;
}

#ifdef SPMI_HW_WITH_RTC
/**
 * mtk_spmi_irq_state - Check PMIC SPMI interrupt status to determine if yocto needs to be notified
 * @rtc: RTC device pointer
 *
 * Iterates through all SPMI slave device interrupt status registers:
 * - For RTC interrupt (SPMI_RTC_IRQ_ID): clears the interrupt here
 * - For other PMIC interrupts: sets return value to 1, indicating that the interrupt needs to be forwarded to yocto
 *
 * Return:
 *   0 - No PMIC interrupt needs to be sent to yocto
 *   1 - There is a PMIC interrupt that needs to be sent to yocto
 */
static int mtk_spmi_irq_state(rtc_device_t* rtc) {
    unsigned int slv_irq_sta = 0;
    int result = 0;

    for (uint32_t index = 0; index < rtc->dev_info.mmio_count; index++) {
        for (int i = 0; i < SPMI_MAX_SLAVE_ID; i++) {
            slv_irq_sta = read_reg(rtc->spmi.spmimst_base_mmio[index], rtc->spmi.spmimst_regs[SPMI_SLV_3_0_EINT + (i / 4)]);
            slv_irq_sta = (slv_irq_sta >> ((i % 4) * 8)) & 0xFF;

            // zxlogf(INFO, "slv_irq_sta:%d, id:%d, index:%d.\n", slv_irq_sta, i, index);
            if (slv_irq_sta && (index == 0) && (i == SPMI_RTC_IRQ_ID)) {
                write_reg(rtc->spmi.spmimst_base_mmio[index], rtc->spmi.spmimst_regs[SPMI_SLV_3_0_EINT + (i / 4)], (0xFF << ((i % 4) * 8)));
            } else if (slv_irq_sta) {
                result = 1;
            }
        }
    }
    zxlogf(INFO, "spmi irq state:%d\n", result);
    return result;
}
#endif

static void rtc_irq_handler(rtc_device_t* rtc, uint32_t irq, uint32_t *call) {
    int status;
    unsigned int index, triggered_index = MAX_VM_COUNT;
    bool schedule_work = false;
    bool need_lock = false;
    bool should_unlock = false;

    if (irq != rtc->irq) {
        zxlogf(ERROR, "%s: %d irq is invaild.\n", __func__, __LINE__);
        return;
    }

    mtx_lock(&rtc->lock);
    if (rtc->wakeup_state == RTC_WAKEUP_IDLE) {
        rtc->wakeup_state = RTC_WAKEUP_IRQ_HELD;
        need_lock = true;
    }
    mtx_unlock(&rtc->lock);
    if (need_lock)
        zx_wakeup_source_lock(rtc->handle);

#ifdef SPMI_HW_WITH_RTC
    *call = mtk_spmi_irq_state(rtc);
#else
    *call = 1;
#endif
    mtx_lock(&rtc->lock);
    status = mtk_rtc_is_alarm_irq(rtc);

#ifdef __IRQ_DEBUG__
    if (((rtc->irq_count / 100) % 2) == 0) {
        zxlogf(INFO, "rtc_irq_handler: status=%d count:%lu\n",
               status, rtc->irq_count);
    }
    rtc->irq_count++;
#endif

    if (status == RTC_NONE) {
        if (rtc->wakeup_state == RTC_WAKEUP_IRQ_HELD) {
            rtc->wakeup_state = RTC_WAKEUP_IDLE;
            should_unlock = true;
        }
        mtx_unlock(&rtc->lock);
        zxlogf(INFO, "irq is not rtc.\n");
        if (should_unlock)
            zx_wakeup_source_unlock(rtc->handle);
        return;
    }

    mtk_rtc_reset_bbpu_alarm_status(rtc);
    if (rtc->wakeup_state == RTC_WAKEUP_IRQ_HELD) {
        rtc->wakeup_state = RTC_WAKEUP_WORK_QUEUED;
        schedule_work = true;
    }
    mtx_unlock(&rtc->lock);

    if (schedule_work) {
        zx_object_signal(rtc->work_event, 0, ZX_USER_SIGNAL_0);
        zxlogf(INFO, "rtc: scheduled rtc alarm  work thread\n");
    }
}

static zx_status_t mtk_rtc_read_time(rtc_device_t* rtc, rtc_t* tm) {
    zx_status_t status;
    uint16_t data[RTC_OFFSET_COUNT] = { 0 };
    uint32_t sec, prev_sec = 0xFF;
    uint32_t bbpu;
    int retry = 0;

    power_on_mclk(rtc);
    zx_nanosleep(zx_deadline_after(ZX_USEC(60)));

    status = rtc_read(rtc, rtc->addr_base + RTC_BBPU, &bbpu);
    if (status != ZX_OK) {
        zxlogf(ERROR, "rtc: failed to read BBPU\n");
        power_down_mclk(rtc);
        return status;
    }
    bbpu = bbpu | RTC_BBPU_KEY | RTC_BBPU_RELOAD;
    status = rtc_write(rtc, rtc->addr_base + RTC_BBPU, bbpu);
    if (status != ZX_OK) {
        zxlogf(ERROR, "rtc: failed to write BBPU\n");
        power_down_mclk(rtc);
        return status;
    }
    status = mtk_rtc_write_trigger(rtc);
    if (status != ZX_OK) {
        zxlogf(ERROR, "rtc: failed to trigger RELOAD\n");
        power_down_mclk(rtc);
        return status;
    }
    power_down_mclk(rtc);

    mtx_lock(&rtc->lock);

    do {
        status = rtc_bulk_read(rtc, rtc->addr_base + RTC_TC_SEC, data, RTC_OFFSET_COUNT);
        if (status != ZX_OK)
            goto exit;

        sec = data[RTC_OFFSET_SEC] & RTC_TC_SEC_MASK;
        if (sec == prev_sec || retry > 10)
            break;

        prev_sec = sec;
        retry++;
        zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
    } while (true);

    tm->seconds = (uint8_t)(data[RTC_OFFSET_SEC] & RTC_TC_SEC_MASK);
    tm->minutes = (uint8_t)(data[RTC_OFFSET_MIN] & RTC_TC_MIN_MASK);
    tm->hours   = (uint8_t)(data[RTC_OFFSET_HOUR] & RTC_TC_HOU_MASK);
    tm->day     = (uint8_t)(data[RTC_OFFSET_DOM] & RTC_TC_DOM_MASK);
    tm->month   = (uint8_t)(data[RTC_OFFSET_MTH] & RTC_TC_MTH_MASK);
    tm->year    = (uint16_t)(data[RTC_OFFSET_YEAR] & RTC_TC_YEA_MASK);

    zxlogf(INFO, "rtc: read time %04d-%02d-%02d %02d:%02d:%02d\n",
           tm->year, tm->month, tm->day, tm->hours, tm->minutes, tm->seconds);

exit:
    mtx_unlock(&rtc->lock);
    return status;
}

static zx_status_t mtk_rtc_set_time(rtc_device_t* rtc, const rtc_t* tm) {
    zx_status_t status;
    uint16_t data[RTC_OFFSET_COUNT];

    zxlogf(INFO, "rtc: set time %04d-%02d-%02d %02d:%02d:%02d\n",
           tm->year + RTC_BASE_YEAR + RTC_MIN_YEAR_OFFSET, tm->month, tm->day, tm->hours, tm->minutes, tm->seconds);

    power_on_mclk(rtc);

    data[RTC_OFFSET_SEC]  = tm->seconds & 0x3F;
    data[RTC_OFFSET_MIN]  = tm->minutes & 0x3F;
    data[RTC_OFFSET_HOUR] = tm->hours & 0x1F;
    data[RTC_OFFSET_DOM]  = tm->day & 0x1F;
    data[RTC_OFFSET_DOW]  = 0;
    data[RTC_OFFSET_MTH]  = tm->month & 0x0F;
    data[RTC_OFFSET_YEAR] = tm->year & 0x7F;

    mtx_lock(&rtc->lock);

    status = rtc_bulk_write(rtc, rtc->addr_base + RTC_TC_SEC, data, RTC_OFFSET_COUNT);
    if (status != ZX_OK)
        goto exit;

    status = mtk_rtc_write_trigger(rtc);
    if (status != ZX_OK)
        goto exit;

    if (!mtk_rtc_check_set_time(rtc, tm, 2, RTC_TC_SEC)) {
        zxlogf(ERROR, "rtc: set time verification failed\n");
        status = ZX_ERR_IO;
        goto exit;
    }

exit:
    mtx_unlock(&rtc->lock);
    power_down_mclk(rtc);
    return status;
}

static void rtc_release(void* ctx) {
    rtc_device_t* rtc = ctx;

    // Stop work thread
    if (rtc->work_thread != 0) {
        rtc->work_stop = true;
        zx_object_signal(rtc->work_event, 0, ZX_USER_SIGNAL_0);
        thrd_join(rtc->work_thread, NULL);
        rtc->work_thread = 0;
        zxlogf(INFO, "rtc: work thread stopped\n");
    }

    if (rtc->work_event != ZX_HANDLE_INVALID) {
        zx_handle_close(rtc->work_event);
        rtc->work_event = ZX_HANDLE_INVALID;
    }

#ifdef RTC_ALARM_SIM_MODE
    mtx_lock(&rtc->alarm_sim_lock);
    if (rtc->alarm_sim_stop != ZX_HANDLE_INVALID) {
        memset(&rtc->alarm_sim_target, 0, sizeof(rtc->alarm_sim_target));
        zx_object_signal(rtc->alarm_sim_stop, 0, ZX_USER_SIGNAL_0);
        zx_handle_t stop = rtc->alarm_sim_stop;
        rtc->alarm_sim_stop = ZX_HANDLE_INVALID;
        mtx_unlock(&rtc->alarm_sim_lock);

        if (rtc->alarm_sim_thread != 0) {
            thrd_join(rtc->alarm_sim_thread, NULL);
            rtc->alarm_sim_thread = 0;
        }

        zx_handle_close(stop);
    } else {
        mtx_unlock(&rtc->alarm_sim_lock);
    }
#endif  // RTC_ALARM_SIM_MODE

    mtx_lock(&rtc_array_lock);
    for (int i = 0; i < MAX_VM_COUNT; i++) {
        if (rtc_array[i].event_handle != ZX_HANDLE_INVALID) {
            zx_handle_close(rtc_array[i].event_handle);
            rtc_array[i].event_handle = ZX_HANDLE_INVALID;
        }
    }
    mtx_unlock(&rtc_array_lock);

    free(rtc);
}

static zx_status_t rtc_ioctl(void* ctx, uint32_t op, const void* in_buf,
                             size_t in_len, void* out_buf, size_t out_len,
                             size_t* out_actual) {
    rtc_device_t* rtc = ctx;

    switch (op) {
    case IOCTL_RTC_GET: {
        if (out_len < sizeof(rtc_t))
            return ZX_ERR_INVALID_ARGS;

        rtc_t* out = (rtc_t*)out_buf;
        zx_status_t status = mtk_rtc_read_time(rtc, out);
        if (status != ZX_OK)
            return status;

        *out_actual = sizeof(rtc_t);
        return ZX_OK;
    }
    case IOCTL_RTC_SET: {
        if (in_len < sizeof(rtc_t))
            return ZX_ERR_INVALID_ARGS;

        const rtc_t* in = (const rtc_t*)in_buf;
        zx_status_t status = mtk_rtc_set_time(rtc, in);
        return status;
    }
    case IOCTL_RTC_GET_ALARM: {
        if (out_len < sizeof(rtc_t) && in_len < sizeof(int32_t))
            return ZX_ERR_INVALID_ARGS;

        rtc_alarm_t* alarm = (const rtc_alarm_t*)out_buf;
        rtc_t *alarm_time = &alarm->alarm_time;
        int32_t vmid = *((int32_t*)in_buf);
        if (vmid >= MAX_VM_COUNT) {
            zxlogf(ERROR, "rtc: invalid VM index %u\n", vmid);
            return ZX_ERR_INVALID_ARGS;
        }

        mtx_lock(&rtc_array_lock);
        *alarm_time = rtc_array[vmid].tm;
        alarm->enabled = !!(rtc_array[vmid].alarm_enabled);
        alarm->pending = 0;
        mtx_unlock(&rtc_array_lock);

        zxlogf(INFO, "rtc: VM[%u] get alarm %04d-%02d-%02d %02d:%02d:%02d\n",
               vmid,
               alarm_time->year + RTC_BASE_YEAR + RTC_MIN_YEAR_OFFSET, alarm_time->month,
               alarm_time->day, alarm_time->hours,
               alarm_time->minutes, alarm_time->seconds);
        *out_actual = sizeof(rtc_alarm_t);
        return ZX_OK;
    }
    case IOCTL_RTC_SET_ALARM: {
        if (in_len < sizeof(rtc_alarm_t))
            return ZX_ERR_INVALID_ARGS;

        const rtc_alarm_t* alarm = (const rtc_alarm_t*)in_buf;

        if (alarm->vm_index >= MAX_VM_COUNT) {
            zxlogf(ERROR, "rtc: invalid VM index %u\n", alarm->vm_index);
            return ZX_ERR_INVALID_ARGS;
        }

        mtx_lock(&rtc_array_lock);

        rtc_array[alarm->vm_index].tm = alarm->alarm_time;
        rtc_array[alarm->vm_index].wake_os_flag = false;
        rtc_array[alarm->vm_index].alarm_enabled = alarm->enabled;

        zxlogf(INFO, "rtc: VM[%u] set alarm %04d-%02d-%02d %02d:%02d:%02d\n",
               alarm->vm_index,
               alarm->alarm_time.year + RTC_BASE_YEAR + RTC_MIN_YEAR_OFFSET, alarm->alarm_time.month,
               alarm->alarm_time.day, alarm->alarm_time.hours,
               alarm->alarm_time.minutes, alarm->alarm_time.seconds);

#ifdef RTC_ALARM_SIM_MODE
        if (rtc->alarm_sim_mode && alarm->enabled) {
            zxlogf(INFO, "rtc: using alarm simulation mode\n");
            start_alarm_sim(rtc, alarm->vm_index, &alarm->alarm_time);
        }

        if (rtc->alarm_sim_mode && !alarm->enabled) {
            zxlogf(INFO, "rtc: stopping alarm simulation\n");
            stop_alarm_sim(rtc);
        }
#endif  // RTC_ALARM_SIM_MODE

        rtc_update_next_alarm(rtc);

        mtx_unlock(&rtc_array_lock);

        return ZX_OK;
    }
    case IOCTL_RTC_GET_EVENT_HANDLE: {
        if (in_len < sizeof(int32_t) || out_len < sizeof(zx_handle_t))
            return ZX_ERR_INVALID_ARGS;

        int32_t vmid = *((int32_t*)in_buf);
        zx_handle_t* out = (zx_handle_t*)out_buf;

        if (vmid < 0 || vmid >= MAX_VM_COUNT) {
            zxlogf(ERROR, "rtc: invalid VM index %d for event handle\n", vmid);
            return ZX_ERR_INVALID_ARGS;
        }

        zx_status_t init_status = init_vm_event_handle(vmid);
        if (init_status != ZX_OK) {
            zxlogf(ERROR, "rtc: failed to initialize VM[%d] event handle\n", vmid);
            return init_status;
        }

        mtx_lock(&rtc_array_lock);
        zx_handle_t event_handle = rtc_array[vmid].event_handle;
        mtx_unlock(&rtc_array_lock);

        if (event_handle == ZX_HANDLE_INVALID) {
            zxlogf(ERROR, "rtc: event handle for VM[%d] is invalid\n", vmid);
            return ZX_ERR_BAD_STATE;
        }

        zx_status_t status = zx_handle_duplicate(event_handle,
                                                ZX_RIGHT_SAME_RIGHTS,
                                                out);
        if (status != ZX_OK) {
            zxlogf(ERROR, "rtc: failed to duplicate event handle for VM[%d]: %d\n", vmid, status);
            return status;
        }

        *out_actual = sizeof(zx_handle_t);
        return ZX_OK;
    }
    case IOCTL_RTC_ALARM_VM: {
        if (in_len < sizeof(int32_t))
            return ZX_ERR_INVALID_ARGS;

        int32_t vmid = *((int32_t*)in_buf);
        if (vmid < YOCTO_VMID || vmid > TBOX_VMID) {
            zxlogf(ERROR, "rtc: invalid VM index %d for event handle\n", vmid);
            return ZX_ERR_INVALID_ARGS;
        }

        if (rtc_array[vmid].event_handle != ZX_HANDLE_INVALID) {
            zx_object_signal(rtc_array[vmid].event_handle, 0, ZX_USER_SIGNAL_0);
            return ZX_OK;
        } else {
            zxlogf(ERROR, "vm event_handle is invalid.");
            return ZX_ERR_BAD_STATE;
        }
    }

    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t rtc_suspend(void* ctx, uint32_t flags) {
    zxlogf(INFO, "rtc suspend\n");
    return ZX_OK;
}

zx_status_t rtc_resume(void* ctx, uint32_t flags) {
    zxlogf(INFO, "rtc resume\n");
    return ZX_OK;
}

static zx_protocol_device_t rtc_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = rtc_ioctl,
    .release = rtc_release,
    .suspend = rtc_suspend,
    .resume = rtc_resume,
};

#ifdef SPMI_HW_WITH_RTC
static zx_status_t spmi_hw_init(rtc_device_t* rtc) {
    zx_status_t status = ZX_OK;

    for(uint32_t i = 0; i < rtc->dev_info.mmio_count; i++) {
        status = pdev_map_mmio(&rtc->pdev, i, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                        &rtc->spmi.spmimst_base_mmio[i], &rtc->spmi.spmimst_mmio_size[i],
                        &rtc->spmi.spmimst_mmio_vmo[i]);
        if (status != ZX_OK)
            break;
    }

    return status;
}

static void spmi_hw_release(rtc_device_t* rtc) {
    for (uint32_t i = 0; i < rtc->dev_info.mmio_count; i++) {
        if (rtc->spmi.spmimst_mmio_vmo[i] != ZX_HANDLE_INVALID) {
            zx_handle_close(rtc->spmi.spmimst_mmio_vmo[i]);
            rtc->spmi.spmimst_base_mmio[i] = NULL;
        }
    }
}
#endif

static zx_status_t rtc_bind(void* ctx, zx_device_t* dev) {
    zx_status_t status;
    uint32_t hwid = 0;

    static bool lock_initialized = false;
    if (!lock_initialized) {
        mtx_init(&rtc_array_lock, mtx_plain);
        lock_initialized = true;
    }

    rtc_device_t* rtc = calloc(1, sizeof(rtc_device_t));
    if (!rtc)
        return ZX_ERR_NO_MEMORY;

    rtc->mclk_counter = 0;
    rtc->wakeup_state = RTC_WAKEUP_IDLE;

    mtx_init(&rtc->lock, mtx_plain);
    mtx_init(&rtc->clk_lock, mtx_plain);
    smc_pmic_init(&rtc->pmic, 0x9);

    rtc->spmi.spmimst_regs = mt6881_spmi_regs;

    // Initialize work thread for deferred task execution
    rtc->work_stop = false;
    status = zx_event_create(0, &rtc->work_event);
    if (status != ZX_OK) {
        zxlogf(ERROR, "rtc: failed to create work event: %d\n", status);
        goto fail;
    }

    int ret = thrd_create_with_name(&rtc->work_thread, rtc_work_thread, rtc, "rtc-work");
    if (ret != thrd_success) {
        zxlogf(ERROR, "rtc: failed to create work thread\n");
        zx_handle_close(rtc->work_event);
        goto fail;
    }

    zxlogf(INFO, "rtc: work thread started\n");

#ifdef RTC_ALARM_SIM_MODE
    rtc->alarm_sim_mode = true;
    rtc->alarm_sim_thread = 0;
    rtc->alarm_sim_stop = ZX_HANDLE_INVALID;
    mtx_init(&rtc->alarm_sim_lock, mtx_plain);
    memset(&rtc->alarm_sim_target, 0, sizeof(rtc->alarm_sim_target));
    zxlogf(INFO, "rtc: alarm simulation mode ENABLED\n");
#endif

    if (device_get_protocol(dev, ZX_PROTOCOL_PLATFORM_DEV, (void*)&rtc->pdev)) {
        zxlogf(ERROR, "%s: device_get_protocol ZX_PROTOCOL_PLATFORM_DEV failed\n", __func__);
        goto fail_thread;
    }

    status = pdev_get_device_info(&rtc->pdev, &rtc->dev_info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_get_device_info failed\n", __func__);
        goto fail_thread;
    }

#ifdef SPMI_HW_WITH_RTC
    status = spmi_hw_init(rtc);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_map_mmio failed\n", __func__);
        goto fail_thread;
    }
#endif
    rtc->addr_base = RTC_DSN_ID;
    rtc->wrtgr = RTC_WRTGR;
    rtc->hwid = HWID_MT6685;
    rtc->chip_version = MT6685_SERIES;
    rtc->irq = RTC_ALARM_IRQ;

    if (!rtc_read(rtc, rtc->hwid, &hwid))
        zxlogf(INFO, "rtc: HWID = 0x%x\n", hwid);
    else
        zxlogf(ERROR, "read rtc hwid failed.\n");

#ifndef RTC_ALARM_SIM_MODE

    if (device_get_protocol(dev, ZX_PROTOCOL_EINT, &rtc->eint_p) != ZX_OK) {
        printf("ERROR: rtc device '%s': does not support eint protocol\n",
               device_get_name(dev));
        goto fail_spmi;
    }
    if (rtc->eint_p.ops->register_irq)
        rtc->eint_p.ops->register_irq(rtc->eint_p.ctx, rtc->irq, GPIO_TRIGGER_HIGH, rtc_irq_handler, (void *)rtc);
    else
        zxlogf(ERROR, "Not found eint protocl\n");
    status = rtc_update_bits(rtc, SCK_TOP_INT_CON0, EN_RTC_INTERRUPT, 1);
    if (status) {
        zxlogf(ERROR, "rtc: enable sck top interrupt failed: %d\n", status);
        goto fail_eint;
    }

#endif
	power_on_mclk(rtc);
	power_down_mclk(rtc);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "mt8668-rtc",
        .ctx = rtc,
        .ops = &rtc_device_proto,
        .flags = DEVICE_ADD_INVISIBLE | DEVICE_ADD_MUST_ISOLATE,
    };

    status = device_add(dev, &args, &rtc->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "rtc: failed to device_add: %d\n", status);
        goto fail_eint;
    }
    device_make_visible(rtc->zxdev);

    const char* name = "rtc-alarm";
    status = zx_wakeup_source_create(name, strlen(name), &rtc->handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "rtc: failed to zx_wakeup_source_create: %d\n", status);
        goto fail_device;
    }

    zxlogf(INFO, "rtc: bound successfully\n");
    return ZX_OK;

fail_device:
    if (rtc->zxdev)
        device_remove(rtc->zxdev);

fail_eint:
#ifndef RTC_ALARM_SIM_MODE
    if (rtc->eint_p.ctx && rtc->eint_p.ops->unregister_irq)
        rtc->eint_p.ops->unregister_irq(rtc->eint_p.ctx, rtc->irq);
#endif

fail_spmi:
#ifdef SPMI_HW_WITH_RTC
    spmi_hw_release(rtc);
#endif

fail_thread:
    rtc->work_stop = true;
    zx_object_signal(rtc->work_event, 0, ZX_USER_SIGNAL_0);
    thrd_join(rtc->work_thread, NULL);
    zx_handle_close(rtc->work_event);

fail:
    free(rtc);
    return status;
}

static zx_driver_ops_t rtc_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = rtc_bind,
};

ZIRCON_DRIVER_BEGIN(rtc-mt6685, rtc_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MTK),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_MT8668),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_GRT_RTC),
ZIRCON_DRIVER_END(rtc-mt6685)
