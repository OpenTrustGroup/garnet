// SPDX-License-Identifier: BSD-3-Clause

#ifndef GARNET_LIB_MACHINA_VIRTIO_RTC_H_
#define GARNET_LIB_MACHINA_VIRTIO_RTC_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <virtio/virtio_ids.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include <zircon/vmid.h>
#include "garnet/lib/machina/virtio_device.h"
#include <zircon/device/rtc.h>

namespace machina {

#define VIRTIO_RTC_Q_COUNT 2

enum {
    VIRTIO_RTC_Q_COMMAND = 0,
    VIRTIO_RTC_Q_EVENT = 1,
};

enum virtio_rtc_cmd {
	VIRTIO_RTC_CMD_READ_TIME = 0,
	VIRTIO_RTC_CMD_SET_TIME,
	VIRTIO_RTC_CMD_READ_ALARM,
	VIRTIO_RTC_CMD_SET_ALARM,
};
#define RTC_MIN_YEAR            1968
#define RTC_BASE_YEAR           1900
#define RTC_NUM_YEARS           128
#define RTC_MIN_YEAR_OFFSET     (RTC_MIN_YEAR - RTC_BASE_YEAR)

#define GUEST_VMID_OFFSET       ZX_VMID_USER_TO_GUEST_OFFSET

struct rtc_time {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

struct rtc_wkalrm {
	unsigned char enabled;	/* 0 = alarm disabled, 1 = alarm enabled */
	unsigned char pending;  /* 0 = alarm not pending, 1 = alarm pending */
	struct rtc_time time;	/* time the alarm is set to */
};


struct virtio_rtc_req {
    uint32_t id;
    uint32_t cmd;
    struct rtc_wkalrm alm;
} __PACKED;

struct virtio_rtc_rsp {
    uint32_t rc;
    struct rtc_wkalrm alm;
} __PACKED;


struct virtio_rtc_event {
    uint32_t pm_state;
    zx_time_t timestamp;
    int32_t vm_index;
} __PACKED;

typedef struct virtio_rtc_config {
    uint8_t rtc_initialized;
    uint8_t reserved[3];
} __PACKED virtio_rtc_config_t;


class VirtioRTC : public VirtioDeviceBase<VIRTIO_ID_RTC, VIRTIO_RTC_Q_COUNT,
                                          virtio_rtc_config_t> {
public:
    VirtioRTC(const PhysMem& phys_mem);
    ~VirtioRTC() override;

    zx_status_t Init(int32_t vmid);

    virtual zx_status_t HandleRTCCommand(VirtioQueue* queue,
                                         uint16_t head,
                                         uint32_t* used);

protected:
    static zx_status_t QueueHandlerCommand(VirtioQueue* queue,
                                           uint16_t head,
                                           uint32_t* used,
                                           void* ctx);

private:
    async::Loop m_rtc_loop;
    async::Loop m_alarm_loop;
    async_t* m_async;
    async_t* m_alarm_async;
    zx::resource root_resource;
    async::Wait command_wait_;
    async::Wait alarm_wait_;
    int rtc_fd_;
    int vmid_;
    zx_handle_t event_handle_;
#ifdef RTC_DEBUG
    thrd_t pm_monitor_thread_;
    bool pm_monitor_running_;
#endif
    zx_status_t OpenRTCDevice();

    bool vm_is_in_suspended(int vmid);

    int32_t UserVmidtoGuestVmid(int32_t user_vmid) {
        return (user_vmid + GUEST_VMID_OFFSET);
    }

    zx_status_t GetEventHandle();

    zx_status_t StartWaitingForAlarm();

    zx_status_t SendAlarmEventToGuest();

    void OnAlarmSignal(async_t* async, zx_status_t status,
                        const zx_packet_signal_t* signal);

    zx_status_t ReadTime(struct rtc_time* time);

    zx_status_t SetTime(const struct rtc_time* time);

    zx_status_t ReadAlarm(struct rtc_wkalrm* alm);

    zx_status_t SetAlarm(struct rtc_wkalrm* alm);

    void RtcToRtcTime(const rtc_t* src, struct rtc_time* dst);

    void RtcTimeToRtc(const struct rtc_time* src, rtc_t* dst);

    static int PmMonitorThread(void* arg);
    void PmMonitorLoop();
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_RTC_H_
