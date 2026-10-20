// SPDX-License-Identifier: BSD-3-Clause

#include "garnet/lib/machina/virtio_rtc.h"
#include <fcntl.h>
#include <iomanip>
#include <string.h>
#include <lib/fxl/logging.h>
#include <zircon/syscalls.h>
#include <zircon/device/rtc.h>
#include <zircon/types.h>
#include <zircon/device/sysinfo.h>

static constexpr char kResourcePath[] = "/dev/misc/sysinfo";
#define GUEST_PM_SYSTEM_OFF          0
#define GUEST_PM_RUNNING             1
#define GUEST_PM_SUSPEND_TO_RAM      2
#define GUEST_PM_SUSPEND_TO_IDLE     3

static zx_status_t get_root_resource(zx::resource* resource) {
    int fd = open(kResourcePath, O_RDWR);
    if (fd < 0)
      return ZX_ERR_IO;
    zx_handle_t rsc_handle;
    ssize_t n = ioctl_sysinfo_get_root_resource(fd, &rsc_handle);
    resource->reset(rsc_handle);
    close(fd);
    return n < 0 ? ZX_ERR_IO : ZX_OK;
}

namespace machina {

#define RTC_DEVICE_PATH "/dev/sys/platform/rtc/mt8668-rtc"
#define YOCTO_VMID      ZX_GUEST_VMID_SOS
#define ANDROID_VMID    ZX_GUEST_VMID_ALPS

VirtioRTC::VirtioRTC(const PhysMem& phys_mem)
    : VirtioDeviceBase(phys_mem, Transport::PCI, false),
      alarm_wait_(){

    zx_status_t status = m_rtc_loop.StartThread("virtio-rtc");
    if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to start virtio-rtc loop: " << status;
        return;
    }
    m_async = m_rtc_loop.async();

    status = m_alarm_loop.StartThread("virtio-rtc-alarm");
    if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to start virtio-rtc-alarm loop: " << status;
        return;
    }
    m_alarm_async = m_alarm_loop.async();

    vmid_ = 0;  // Default value, will be set by Init()

    status = get_root_resource(&root_resource);
    if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to get hypervisor resource";
        return;
    }

    status = OpenRTCDevice();
    if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to open RTC device: " << status;
        config_.rtc_initialized = 0;
    } else {
        config_.rtc_initialized = 1;
    }

    status = queue(VIRTIO_RTC_Q_COMMAND)->PollAsync(
        m_async, &command_wait_, &VirtioRTC::QueueHandlerCommand, this);
    if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to PollAsync command queue: " << status;
        return;
    }

    FXL_LOG(INFO) << "VirtioRTC constructed, Init() needs to be called";
}

VirtioRTC::~VirtioRTC() {
#ifdef RTC_DEBUG
    if (pm_monitor_running_) {
        pm_monitor_running_ = false;
        thrd_join(pm_monitor_thread_, nullptr);
    }
#endif
    m_alarm_loop.Shutdown();
    m_rtc_loop.Shutdown();

    if (event_handle_ != ZX_HANDLE_INVALID) {
        zx_handle_close(event_handle_);
        event_handle_ = ZX_HANDLE_INVALID;
    }
    if (rtc_fd_ >= 0) {
        close(rtc_fd_);
        rtc_fd_ = -1;
    }
    FXL_LOG(INFO) << "VirtioRTC destroyed";
}

zx_status_t VirtioRTC::Init(int32_t vmid) {

    vmid_ = UserVmidtoGuestVmid(vmid);

    zx_status_t status = GetEventHandle();
    if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to get event handle: " << status;
        return status;
    } else {
        status = StartWaitingForAlarm();
        if (status != ZX_OK) {
            FXL_LOG(ERROR) << "Failed to start waiting for alarm: " << status;
            return status;
        }
    }

#ifdef RTC_DEBUG
    pm_monitor_running_ = true;
    int ret = thrd_create_with_name(&pm_monitor_thread_,
                                     &VirtioRTC::PmMonitorThread,
                                     this,
                                     "virtio-rtc-pm-monitor");
    if (ret != thrd_success) {
        FXL_LOG(ERROR) << "Failed to create PM monitor thread";
        pm_monitor_running_ = false;
        return ZX_ERR_NO_RESOURCES;
    }
#endif
    FXL_LOG(INFO) << "VirtioRTC::Init called with vmid: " << vmid
                  << ", vmid_=" << vmid_;
    return ZX_OK;
}

zx_status_t VirtioRTC::OpenRTCDevice() {
    rtc_fd_ = open(RTC_DEVICE_PATH, O_RDWR);
    if (rtc_fd_ < 0) {
        FXL_LOG(ERROR) << "Failed to open RTC device: " << RTC_DEVICE_PATH;
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

bool VirtioRTC::vm_is_in_suspended(int vmid) {
    int32_t pm_state = 0;

    zx_get_pmstate_form_vmid(root_resource.get(), vmid - GUEST_VMID_OFFSET, &pm_state);
    FXL_LOG(ERROR) << "get pm state: " << pm_state << " from vmid: " << vmid;
    if (pm_state == GUEST_PM_SUSPEND_TO_RAM ||
        pm_state == GUEST_PM_SUSPEND_TO_IDLE) {
        return true;
    }
    return false;
}

void VirtioRTC::RtcToRtcTime(const rtc_t* src, struct rtc_time* dst) {
    dst->tm_sec = src->seconds;
    dst->tm_min = src->minutes;
    dst->tm_hour = src->hours;
    dst->tm_mday = src->day;
    dst->tm_mon = src->month;
    dst->tm_year = src->year;
    dst->tm_wday = 0;
    dst->tm_yday = 0;
    dst->tm_isdst = -1;
}

void VirtioRTC::RtcTimeToRtc(const struct rtc_time* src, rtc_t* dst) {
    dst->seconds = src->tm_sec;
    dst->minutes = src->tm_min;
    dst->hours = src->tm_hour;
    dst->day = src->tm_mday;
    dst->month = src->tm_mon;
    dst->year = src->tm_year;
}

zx_status_t VirtioRTC::ReadTime(struct rtc_time* time) {
    if (!time) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (rtc_fd_ < 0) {
        FXL_LOG(ERROR) << "RTC device not open";
        return ZX_ERR_NOT_FOUND;
    }
	FXL_LOG(INFO) << "Read rtc time vmid: " << vmid_;
    rtc_t rtc_time;
    ssize_t ret = ioctl_rtc_get(rtc_fd_, &rtc_time);
    if (ret < 0) {
        FXL_LOG(ERROR) << "Failed to read RTC time: " << ret;
        return ZX_ERR_IO;
    }

    RtcToRtcTime(&rtc_time, time);

    return ZX_OK;
}

zx_status_t VirtioRTC::SetTime(const struct rtc_time* time) {
    if (!time) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (rtc_fd_ < 0) {
        FXL_LOG(ERROR) << "RTC device not open";
        return ZX_ERR_NOT_FOUND;
    }

	FXL_LOG(INFO) << "Set rtc time vmid: " << vmid_;
    rtc_t rtc_time;
    RtcTimeToRtc(time, &rtc_time);

    ssize_t ret = ioctl_rtc_set(rtc_fd_, &rtc_time);
    if (ret < 0) {
        FXL_LOG(ERROR) << "Failed to set RTC time: " << ret;
        return ZX_ERR_IO;
    }

    return ZX_OK;
}

zx_status_t VirtioRTC::ReadAlarm(struct rtc_wkalrm* alm) {
    if (!alm) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (rtc_fd_ < 0) {
        FXL_LOG(ERROR) << "RTC device not open";
        return ZX_ERR_NOT_FOUND;
    }

    rtc_alarm_t alarm;
    int32_t vmid = vmid_;
    FXL_LOG(INFO) << "Read alarm time vmid: " << vmid;

    ssize_t ret = ioctl_rtc_get_alarm(rtc_fd_, &vmid, &alarm);
    if (ret < 0) {
        FXL_LOG(ERROR) << "Failed to read RTC time: " << ret;
        return ZX_ERR_IO;
    }

    RtcToRtcTime(&alarm.alarm_time, &alm->time);
    alm->enabled = alarm.enabled;
    alm->pending = alarm.pending;

    return ZX_OK;
}

zx_status_t VirtioRTC::SetAlarm(struct rtc_wkalrm* alm) {
    if (!alm) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (rtc_fd_ < 0) {
        FXL_LOG(ERROR) << "RTC device not open";
        return ZX_ERR_NOT_FOUND;
    }

    rtc_alarm_t alarm;
    alarm.vm_index = vmid_;

    rtc_t rtc_time;
    RtcTimeToRtc(&alm->time, &rtc_time);
    alarm.alarm_time = rtc_time;
    alarm.enabled = alm->enabled;

    ssize_t ret = ioctl_rtc_set_alarm(rtc_fd_, &alarm);
    if (ret < 0) {
        FXL_LOG(ERROR) << "Failed to set RTC alarm: " << ret;
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t VirtioRTC::QueueHandlerCommand(VirtioQueue* queue,
                                             uint16_t head,
                                             uint32_t* used,
                                             void* ctx) {
    VirtioRTC* rtc = reinterpret_cast<VirtioRTC*>(ctx);
    return rtc->HandleRTCCommand(queue, head, used);
}

zx_status_t VirtioRTC::HandleRTCCommand(VirtioQueue* queue,
                                        uint16_t head,
                                        uint32_t* used) {
    virtio_desc_t req_desc;
    virtio_desc_t resp_desc;
    struct virtio_rtc_req* req;
    struct virtio_rtc_rsp* resp;
    zx_status_t status = ZX_OK;

    queue->ReadDesc(head, &req_desc);
    if (req_desc.addr == nullptr) {
        FXL_LOG(ERROR) << "Invalid request descriptor";
        return ZX_ERR_IO_DATA_INTEGRITY;
    }

    req = reinterpret_cast<struct virtio_rtc_req*>(req_desc.addr);

    if (!req_desc.has_next) {
        FXL_LOG(ERROR) << "Request descriptor missing response descriptor";
        return ZX_ERR_IO_DATA_INTEGRITY;
    }

    queue->ReadDesc(req_desc.next, &resp_desc);
    if (resp_desc.addr == nullptr) {
        FXL_LOG(ERROR) << "Invalid response descriptor";
        return ZX_ERR_IO_DATA_INTEGRITY;
    }

    resp = reinterpret_cast<struct virtio_rtc_rsp*>(resp_desc.addr);

    resp->rc = 0;

    switch (req->cmd) {
    case VIRTIO_RTC_CMD_READ_TIME: {
        struct rtc_time tm;
        status = ReadTime(&tm);
        memcpy(&resp->alm.time, &tm, sizeof(tm));
        if (status != ZX_OK) {
            resp->rc = status;
            FXL_LOG(ERROR) << "[BE] RTC read time failed: " << status;
        } else {
            FXL_LOG(INFO) << "[BE] RTC read time: "
                          << tm.tm_year + RTC_BASE_YEAR + RTC_MIN_YEAR_OFFSET << "-"
                          << std::setw(2) << std::setfill('0') << tm.tm_mon << "-"
                          << std::setw(2) << std::setfill('0') << tm.tm_mday << " "
                          << std::setw(2) << std::setfill('0') << tm.tm_hour << ":"
                          << std::setw(2) << std::setfill('0') << tm.tm_min << ":"
                          << std::setw(2) << std::setfill('0') << tm.tm_sec
                          << " (id:" << req->id << ")";
        }
        break;
    }

    case VIRTIO_RTC_CMD_SET_TIME: {
        struct rtc_time tm;
        memcpy(&tm, &req->alm.time, sizeof(struct rtc_time));
        status = SetTime(&tm);
        if (status != ZX_OK) {
            resp->rc = status;
            FXL_LOG(ERROR) << "[BE] RTC set time failed: " << status;
        } else {
            FXL_LOG(INFO) << "[BE] RTC set time: "
                          << tm.tm_year + RTC_BASE_YEAR + RTC_MIN_YEAR_OFFSET << "-"
                          << std::setw(2) << std::setfill('0') << tm.tm_mon << "-"
                          << std::setw(2) << std::setfill('0') << tm.tm_mday << " "
                          << std::setw(2) << std::setfill('0') << tm.tm_hour << ":"
                          << std::setw(2) << std::setfill('0') << tm.tm_min << ":"
                          << std::setw(2) << std::setfill('0') << tm.tm_sec
                          << " (id:" << req->id << ")";
        }
        break;
    }

    case VIRTIO_RTC_CMD_READ_ALARM: {
        struct rtc_wkalrm alm;
        status = ReadAlarm(&alm);
        memcpy(&resp->alm, &alm, sizeof(alm));
        if (status != ZX_OK) {
            resp->rc = status;
            FXL_LOG(ERROR) << "[BE] RTC read alarm failed: " << status;
        } else {
            FXL_LOG(INFO) << "[BE] RTC read alarm: "
                          << alm.time.tm_year + RTC_BASE_YEAR + RTC_MIN_YEAR_OFFSET << "-"
                          << std::setw(2) << std::setfill('0') << alm.time.tm_mon << "-"
                          << std::setw(2) << std::setfill('0') << alm.time.tm_mday << " "
                          << std::setw(2) << std::setfill('0') << alm.time.tm_hour << ":"
                          << std::setw(2) << std::setfill('0') << alm.time.tm_min << ":"
                          << std::setw(2) << std::setfill('0') << alm.time.tm_sec
                          << " (id:" << req->id << ")";
        }
        break;
    }

    case VIRTIO_RTC_CMD_SET_ALARM: {
        struct rtc_wkalrm alm;
        memcpy(&alm, &req->alm, sizeof(struct rtc_wkalrm));
        status = SetAlarm(&alm);
        if (status != ZX_OK) {
            resp->rc = status;
            FXL_LOG(ERROR) << "[BE] RTC set alarm failed: " << status;
        } else {
            FXL_LOG(INFO) << "[BE] RTC set alarm: "
                          << alm.time.tm_year + RTC_BASE_YEAR + RTC_MIN_YEAR_OFFSET << "-"
                          << std::setw(2) << std::setfill('0') << alm.time.tm_mon << "-"
                          << std::setw(2) << std::setfill('0') << alm.time.tm_mday << " "
                          << std::setw(2) << std::setfill('0') << alm.time.tm_hour << ":"
                          << std::setw(2) << std::setfill('0') << alm.time.tm_min << ":"
                          << std::setw(2) << std::setfill('0') << alm.time.tm_sec
                          << " (id:" << req->id << ")";
        }
        break;
    }

    default:
        FXL_LOG(ERROR) << "[BE] Unknown RTC command: " << req->cmd << " (id:" << req->id << ")";
        resp->rc = ZX_ERR_NOT_SUPPORTED;
        break;
    }

    *used += sizeof(struct virtio_rtc_rsp);
    return ZX_OK;
}

zx_status_t VirtioRTC::GetEventHandle() {
    if (rtc_fd_ < 0) {
        FXL_LOG(ERROR) << "RTC device not open";
        return ZX_ERR_NOT_FOUND;
    }

    zx_handle_t event_handle;
    int vmid = vmid_;
    ssize_t ret = ioctl_rtc_get_event_handle(rtc_fd_, &vmid, &event_handle);
    if (ret < 0) {
        FXL_LOG(ERROR) << "Failed to get event handle for VM["
                       << vmid_ << "]: " << ret;
        return ZX_ERR_IO;
    }

    event_handle_ = event_handle;
    FXL_LOG(INFO) << "Got RTC event handle for VM[" << vmid_
                  << "]: " << event_handle_;
    return ZX_OK;
}

zx_status_t VirtioRTC::StartWaitingForAlarm() {
    if (event_handle_ == ZX_HANDLE_INVALID) {
        FXL_LOG(ERROR) << "Event handle not initialized";
        return ZX_ERR_BAD_STATE;
    }

    zx_status_t cancel_status = alarm_wait_.Cancel(m_alarm_async);
    if (cancel_status != ZX_OK && cancel_status != ZX_ERR_NOT_FOUND) {
        FXL_LOG(WARNING) << "Cancel wait failed: " << cancel_status;
    }

    zx_object_signal(event_handle_, ZX_USER_SIGNAL_0, 0);

    alarm_wait_.set_object(event_handle_);
    alarm_wait_.set_trigger(ZX_USER_SIGNAL_0);
    alarm_wait_.set_handler([this](async_t* async, zx_status_t status,
                                    const zx_packet_signal_t* signal) {
        OnAlarmSignal(async, status, signal);
        return ASYNC_WAIT_AGAIN;
    });

    zx_status_t status = alarm_wait_.Begin(m_alarm_async);
    if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to begin alarm wait: " << status;
        return status;
    }

    FXL_LOG(INFO) << "Started waiting for RTC alarm events";
    return ZX_OK;
}

void VirtioRTC::OnAlarmSignal(async_t* async, zx_status_t status,
                               const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Alarm wait error: " << status;
        return;
    }

    if (signal->observed & ZX_USER_SIGNAL_0) {
        zx_object_signal(event_handle_, ZX_USER_SIGNAL_0, 0);

        zx_status_t status = SendAlarmEventToGuest();
        if (status != ZX_OK) {
            FXL_LOG(ERROR) << "Failed to send alarm event to guest: " << status;
        }
    }
}

zx_status_t VirtioRTC::SendAlarmEventToGuest() {
    VirtioQueue* q = queue(VIRTIO_RTC_Q_EVENT);
    if (!q) {
        FXL_LOG(ERROR) << "Event queue not available";
        return ZX_ERR_BAD_STATE;
    }
    bool pm_state = vm_is_in_suspended(vmid_);  // 0 = running, 1 = suspend

    // When waking up Android, wake up Yocto at the same time
    if ((vmid_ == ANDROID_VMID) && pm_state && (rtc_fd_ >= 0)) {
        int vmid = YOCTO_VMID;
        if (vm_is_in_suspended(vmid)) {
            ssize_t ret = ioctl_rtc_alarm_vm(rtc_fd_, &vmid);
            if (ret < 0) {
                FXL_LOG(ERROR) << "Failed to send rtc alarm to yocto" << ret;
                return ZX_ERR_IO;
            }
            FXL_LOG(INFO) << "Send rtc alarm to yocto";
        }
    }

    struct virtio_rtc_event event = {};
    event.pm_state = pm_state;
    event.timestamp = zx_clock_get(ZX_CLOCK_MONOTONIC);
    event.vm_index = vmid_;

    FXL_LOG(INFO) << "=== RTC Alarm Triggered! === VM[" << vmid_
                  << "], timespame: " << event.timestamp
                  << ", pm state:" << pm_state;
    uint16_t head;
    zx_status_t status = q->NextAvail(&head);
    if (status == ZX_ERR_SHOULD_WAIT) {
        FXL_LOG(WARNING) << "Event queue full, dropping alarm event";
        return ZX_ERR_SHOULD_WAIT;
    }
    if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to get available descriptor: " << status;
        return status;
    }

    virtio_desc_t desc;
    status = q->ReadDesc(head, &desc);
    if (status != ZX_OK) {
        q->Return(head, 0);
        return status;
    }

    if (desc.len < sizeof(event)) {
        FXL_LOG(ERROR) << "Event descriptor too small: " << desc.len
                       << " (required: " << sizeof(event) << ")";
        q->Return(head, 0);
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(desc.addr, &event, sizeof(event));

    return q->Return(head, sizeof(event),
                    VirtioQueue::InterruptAction::SEND_INTERRUPT);
}
#ifdef RTC_DEBUG
int VirtioRTC::PmMonitorThread(void* arg) {
    VirtioRTC* rtc = static_cast<VirtioRTC*>(arg);
    rtc->PmMonitorLoop();
    return 0;
}

void VirtioRTC::PmMonitorLoop() {
    FXL_LOG(INFO) << "PM monitor thread started for VM[" << vmid_ << "]";

    while (pm_monitor_running_) {
        bool suspended = vm_is_in_suspended(vmid_);
        FXL_LOG(INFO) << "[PM Monitor] VM[" << vmid_ << "] PM state: "
                      << (suspended ? "SUSPENDED" : "RUNNING");

        zx_nanosleep(zx_deadline_after(ZX_SEC(5)));
    }

    FXL_LOG(INFO) << "PM monitor thread exiting for VM[" << vmid_ << "]";
}
#endif
}  // namespace machina
