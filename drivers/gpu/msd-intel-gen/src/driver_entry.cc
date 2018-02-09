// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zx/channel.h"
#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/display.h>
#include <ddk/protocol/intel-gpu-core.h>
#include <ddk/protocol/pci.h>
#include <hw/pci.h>

#include <atomic>
#include <set>
#include <thread>
#include <zircon/process.h>
#include <zircon/types.h>

#include "magma_util/cache_flush.h"
#include "magma_util/dlog.h"
#include "magma_util/platform/zircon/zircon_platform_ioctl.h"
#include "magma_util/platform/zircon/zircon_platform_trace.h"
#include "sys_driver/magma_driver.h"
#include "sys_driver/magma_system_buffer.h"

#include "core/msd_intel_device_core.h"
#include "msd_intel_buffer.h"
#include "msd_intel_pci_device.h"
#include "msd_intel_semaphore.h"

#if MAGMA_TEST_DRIVER
void magma_indriver_test(magma::PlatformPciDevice* platform_device, void* core_device);
#endif

struct sysdrv_device_t {
    zx_device_t* parent_device;
    zx_device_t* zx_device_display;
    zx_device_t* zx_device_gpu;

    void* framebuffer_addr;
    uint64_t framebuffer_size;

    zx_display_info_t info;
    uint32_t flags;

    zx_display_cb_t ownership_change_callback{nullptr};
    void* ownership_change_cookie{nullptr};

    std::unique_ptr<MsdIntelDeviceCore> core_device;
    zx_intel_gpu_core_protocol_t gpu_core_protocol;
    std::set<uint64_t> client_gtt_allocations;
    std::unordered_map<uint32_t, std::unique_ptr<magma::PlatformMmio>> client_mmio_allocations;

    std::unique_ptr<magma::PlatformBuffer> console_buffer;
    std::unique_ptr<magma::PlatformBuffer> placeholder_buffer;
    std::unique_ptr<MagmaDriver> magma_driver;
    std::shared_ptr<MagmaSystemDevice> magma_system_device;
    std::shared_ptr<MagmaSystemBuffer> console_framebuffer;
    std::shared_ptr<MagmaSystemBuffer> placeholder_framebuffer;
    std::mutex magma_mutex;
    std::atomic_bool console_visible{true};
};

static magma::CacheFlush g_cache_flush;

static int magma_start(sysdrv_device_t* dev);

#if MAGMA_TEST_DRIVER
static int magma_stop(sysdrv_device_t* dev);
#endif

sysdrv_device_t* get_device(void* context) { return static_cast<sysdrv_device_t*>(context); }

// implement core device protocol
static zx_status_t read_pci_config_16(void* ctx, uint64_t addr, uint16_t* value_out)
{
    if (!get_device(ctx)->core_device->platform_device()->ReadPciConfig16(addr, value_out))
        return DRET_MSG(ZX_ERR_INTERNAL, "ReadPciConfig16 failed");
    return ZX_OK;
}

static zx_status_t map_pci_mmio(void* ctx, uint32_t pci_bar, void** addr_out, uint64_t* size_out)
{
    std::unique_ptr<magma::PlatformMmio> platform_mmio =
        get_device(ctx)->core_device->platform_device()->CpuMapPciMmio(
            pci_bar, magma::PlatformMmio::CachePolicy::CACHE_POLICY_UNCACHED_DEVICE);
    if (!platform_mmio)
        return DRET_MSG(ZX_ERR_INTERNAL, "CpuMapPciMmio failed");

    *addr_out = platform_mmio->addr();
    *size_out = platform_mmio->size();
    get_device(ctx)->client_mmio_allocations[pci_bar] = std::move(platform_mmio);

    return ZX_OK;
}

static zx_status_t unmap_pci_mmio(void* ctx, uint32_t pci_bar)
{
    if (get_device(ctx)->client_mmio_allocations.erase(pci_bar) != 1)
        return DRET_MSG(ZX_ERR_INTERNAL, "failed to erase mmio mapping for pci_bar %d", pci_bar);
    return ZX_OK;
}

static zx_status_t register_interrupt_callback(void* ctx,
                                               zx_intel_gpu_core_interrupt_callback_t callback,
                                               void* data, uint32_t interrupt_mask)
{
    if (!get_device(ctx)->core_device->RegisterCallback(callback, data, interrupt_mask))
        return DRET_MSG(ZX_ERR_INTERNAL, "RegisterCallback failed");
    return ZX_OK;
}

static zx_status_t unregister_interrupt_callback(void* ctx)
{
    get_device(ctx)->core_device->UnregisterCallback();
    return ZX_OK;
}

static uint64_t gtt_get_size(void* ctx) { return get_device(ctx)->core_device->gtt()->Size(); }

static zx_status_t gtt_alloc(void* ctx, uint64_t size, uint64_t* addr_out)
{
    if (!get_device(ctx)->core_device->gtt()->Alloc(size * PAGE_SIZE, 0, addr_out))
        return DRET_MSG(ZX_ERR_INTERNAL, "Alloc failed");
    get_device(ctx)->client_gtt_allocations.insert(*addr_out);
    return ZX_OK;
}

static zx_status_t gtt_free(void* ctx, uint64_t addr)
{
    if (!get_device(ctx)->core_device->gtt()->Free(addr))
        return DRET_MSG(ZX_ERR_INTERNAL, "Free failed");
    get_device(ctx)->client_gtt_allocations.erase(addr);
    return ZX_OK;
}

static zx_status_t gtt_clear(void* ctx, uint64_t addr)
{
    if (!get_device(ctx)->core_device->gtt()->Clear(addr))
        return DRET_MSG(ZX_ERR_INTERNAL, "Clear failed");
    return ZX_OK;
}

static zx_status_t gtt_insert(void* ctx, uint64_t addr, zx_handle_t buffer, uint64_t page_offset,
                              uint64_t page_count)
{
    if (!get_device(ctx)->core_device->gtt()->Insert(addr, buffer, page_offset * PAGE_SIZE,
                                                     page_count * PAGE_SIZE, CACHING_LLC))
        return DRET_MSG(ZX_ERR_INTERNAL, "Insert failed");
    return ZX_OK;
}

static zx_intel_gpu_core_protocol_ops_t gpu_core_protocol_ops = {
    .read_pci_config_16 = read_pci_config_16,
    .map_pci_mmio = map_pci_mmio,
    .unmap_pci_mmio = unmap_pci_mmio,
    .register_interrupt_callback = register_interrupt_callback,
    .unregister_interrupt_callback = unregister_interrupt_callback,
    .gtt_get_size = gtt_get_size,
    .gtt_alloc = gtt_alloc,
    .gtt_free = gtt_free,
    .gtt_clear = gtt_clear,
    .gtt_insert = gtt_insert};

// implement display protocol

static zx_status_t sysdrv_display_set_mode(void* ctx, zx_display_info_t* info)
{
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t sysdrv_display_get_mode(void* ctx, zx_display_info_t* info)
{
    assert(info);
    sysdrv_device_t* device = get_device(ctx);
    memcpy(info, &device->info, sizeof(zx_display_info_t));
    return ZX_OK;
}

static zx_status_t sysdrv_display_get_framebuffer(void* ctx, void** framebuffer)
{
    assert(framebuffer);
    sysdrv_device_t* device = get_device(ctx);
    (*framebuffer) = device->framebuffer_addr;
    return ZX_OK;
}

static void sysdrv_display_flush(void* ctx)
{
    sysdrv_device_t* device = get_device(ctx);
    // Don't incur overhead of flushing when console's not visible
    if (device->console_visible)
        g_cache_flush.clflush_range(device->framebuffer_addr, device->framebuffer_size);
}

static void sysdrv_display_acquire_or_release_display(void* ctx, bool acquire)
{
    sysdrv_device_t* device = get_device(ctx);
    DLOG("sysdrv_i915_acquire_or_release_display");

    std::unique_lock<std::mutex> lock(device->magma_mutex);

    if (acquire && device->magma_system_device->page_flip_enabled()) {
        DLOG("flipping to console");
        // Ensure any software writes to framebuffer are visible
        device->console_visible = true;
        if (device->ownership_change_callback)
            device->ownership_change_callback(true, device->ownership_change_cookie);
        g_cache_flush.clflush_range(device->framebuffer_addr, device->framebuffer_size);
        magma_system_image_descriptor image_desc{MAGMA_IMAGE_TILING_LINEAR};
        auto last_framebuffer = device->magma_system_device->PageFlipAndEnable(
            device->console_framebuffer, &image_desc, false);
        if (last_framebuffer) {
            void* data;
            if (last_framebuffer->platform_buffer()->MapCpu(&data)) {
                g_cache_flush.clflush_range(data, last_framebuffer->size());
                last_framebuffer->platform_buffer()->UnmapCpu();
            }
            device->placeholder_framebuffer = last_framebuffer;
        }
    } else if (!acquire && !device->magma_system_device->page_flip_enabled()) {
        DLOG("flipping to placeholder_framebuffer");
        magma_system_image_descriptor image_desc{MAGMA_IMAGE_TILING_OPTIMAL};
        device->magma_system_device->PageFlipAndEnable(device->placeholder_framebuffer, &image_desc,
                                                       true);
        device->console_visible = false;
        if (device->ownership_change_callback)
            device->ownership_change_callback(false, device->ownership_change_cookie);
    }
}

static void sysdrv_display_set_ownership_change_callback(void* ctx, zx_display_cb_t callback,
                                                        void* cookie)
{
    sysdrv_device_t* device = get_device(ctx);
    std::unique_lock<std::mutex> lock(device->magma_mutex);
    device->ownership_change_callback = callback;
    device->ownership_change_cookie = cookie;
}

static display_protocol_ops_t sysdrv_display_proto = {
    .set_mode = sysdrv_display_set_mode,
    .get_mode = sysdrv_display_get_mode,
    .get_framebuffer = sysdrv_display_get_framebuffer,
    .acquire_or_release_display = sysdrv_display_acquire_or_release_display,
    .set_ownership_change_callback = sysdrv_display_set_ownership_change_callback,
    .flush = sysdrv_display_flush,
};

// implement device protocol

static zx_status_t sysdrv_common_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                      void* out_buf, size_t out_len, size_t* out_actual)
{
    sysdrv_device_t* device = get_device(ctx);

    DASSERT(device->magma_system_device);

    zx_status_t result = ZX_ERR_NOT_SUPPORTED;

    switch (op) {
        case IOCTL_MAGMA_QUERY: {
            DLOG("IOCTL_MAGMA_QUERY");
            const uint64_t* param = reinterpret_cast<const uint64_t*>(in_buf);
            if (!in_buf || in_len < sizeof(*param))
                return DRET_MSG(ZX_ERR_INVALID_ARGS, "bad in_buf");
            uint64_t* value_out = reinterpret_cast<uint64_t*>(out_buf);
            if (!out_buf || out_len < sizeof(*value_out))
                return DRET_MSG(ZX_ERR_INVALID_ARGS, "bad out_buf");
            switch (*param) {
                case MAGMA_QUERY_DEVICE_ID:
                    *value_out = device->magma_system_device->GetDeviceId();
                    break;
                default:
                    if (!device->magma_system_device->Query(*param, value_out))
                        return DRET_MSG(ZX_ERR_INVALID_ARGS, "unhandled param 0x%" PRIx64,
                                        *value_out);
            }
            DLOG("query param 0x%" PRIx64 " returning 0x%" PRIx64, *param, *value_out);
            *out_actual = sizeof(*value_out);
            result = ZX_OK;
            break;
        }

        case IOCTL_MAGMA_DUMP_STATUS: {
            DLOG("IOCTL_MAGMA_DUMP_STATUS");
            std::unique_lock<std::mutex> lock(device->magma_mutex);
            sysdrv_device_t* device = get_device(ctx);
            if (device->magma_system_device)
                device->magma_system_device->DumpStatus();
            result = ZX_OK;
            break;
        }

#if MAGMA_TEST_DRIVER
        case IOCTL_MAGMA_TEST_RESTART: {
            DLOG("IOCTL_MAGMA_TEST_RESTART");
            std::unique_lock<std::mutex> lock(device->magma_mutex);
            result = magma_stop(device);
            if (result != ZX_OK)
                return DRET_MSG(result, "magma_stop failed");
            result = magma_start(device);
            break;
        }
#endif
    }

    return result;
}

static zx_status_t sysdrv_gpu_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                   void* out_buf, size_t out_len, size_t* out_actual)
{
    DLOG("sysdrv_gpu_ioctl");
    zx_status_t result = sysdrv_common_ioctl(ctx, op, in_buf, in_len, out_buf, out_len, out_actual);
    if (result == ZX_OK)
        return ZX_OK;

    if (result != ZX_ERR_NOT_SUPPORTED)
        return result;

    sysdrv_device_t* device = get_device(ctx);
    DASSERT(device->magma_system_device);

    switch (op) {
        case IOCTL_MAGMA_CONNECT: {
            DLOG("IOCTL_MAGMA_CONNECT");
            auto request = reinterpret_cast<const magma_system_connection_request*>(in_buf);
            if (!in_buf || in_len < sizeof(*request))
                return DRET(ZX_ERR_INVALID_ARGS);

            auto device_handle_out = reinterpret_cast<uint32_t*>(out_buf);
            if (!out_buf || out_len < sizeof(*device_handle_out) * 2)
                return DRET(ZX_ERR_INVALID_ARGS);

            if ((request->capabilities & MAGMA_CAPABILITY_DISPLAY) ||
                (request->capabilities & MAGMA_CAPABILITY_RENDERING) == 0)
                return DRET(ZX_ERR_INVALID_ARGS);

            auto connection = MagmaSystemDevice::Open(
                device->magma_system_device, request->client_id, MAGMA_CAPABILITY_RENDERING);
            if (!connection)
                return DRET(ZX_ERR_INVALID_ARGS);

            device_handle_out[0] = connection->GetHandle();
            device_handle_out[1] = connection->GetNotificationChannel();
            *out_actual = sizeof(*device_handle_out) * 2;
            result = ZX_OK;

            device->magma_system_device->StartConnectionThread(std::move(connection));

            break;
        }

        default:
            DLOG("sysdrv_gpu_ioctl unhandled op 0x%x", op);
    }

    return result;
}

static int reset_placeholder(sysdrv_device_t* device)
{
    void* addr;
    if (device->placeholder_buffer->MapCpu(&addr)) {
        memset(addr, 0, device->placeholder_buffer->size());
        g_cache_flush.clflush_range(addr, device->placeholder_buffer->size());
        device->placeholder_buffer->UnmapCpu();
    }

    uint32_t buffer_handle;
    if (!device->placeholder_buffer->duplicate_handle(&buffer_handle))
        return DRET_MSG(ZX_ERR_NO_RESOURCES, "duplicate_handle failed");

    device->placeholder_framebuffer =
        MagmaSystemBuffer::Create(magma::PlatformBuffer::Import(buffer_handle));
    if (!device->placeholder_framebuffer)
        return DRET_MSG(ZX_ERR_NO_MEMORY, "failed to created magma system buffer");

    return ZX_OK;
}

static zx_status_t sysdrv_display_ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                                       void* out_buf, size_t out_len, size_t* out_actual)
{
    DLOG("sysdrv_display_ioctl");
    zx_status_t result = sysdrv_common_ioctl(ctx, op, in_buf, in_len, out_buf, out_len, out_actual);
    if (result == ZX_OK)
        return ZX_OK;

    if (result != ZX_ERR_NOT_SUPPORTED)
        return result;

    sysdrv_device_t* device = get_device(ctx);
    DASSERT(device->magma_system_device);

    switch (op) {
        case IOCTL_DISPLAY_GET_FB: {
            DLOG("MAGMA IOCTL_DISPLAY_GET_FB");
            if (out_len < sizeof(ioctl_display_get_fb_t))
                return DRET(ZX_ERR_INVALID_ARGS);
            ioctl_display_get_fb_t* description = static_cast<ioctl_display_get_fb_t*>(out_buf);
            device->console_buffer->duplicate_handle(
                reinterpret_cast<uint32_t*>(&description->vmo));
            description->info = device->info;
            *out_actual = sizeof(ioctl_display_get_fb_t);
            result = ZX_OK;
            break;
        }

        case IOCTL_MAGMA_DISPLAY_GET_SIZE: {
            DLOG("IOCTL_MAGMA_DISPLAY_GET_SIZE");
            if (in_len != 0)
                return DRET_MSG(ZX_ERR_INVALID_ARGS, "bad in_buf");
            auto* value_out = static_cast<magma_display_size*>(out_buf);
            if (!out_buf || out_len < sizeof(*value_out))
                return DRET_MSG(ZX_ERR_INVALID_ARGS, "bad out_buf");

            std::unique_lock<std::mutex> lock(device->magma_mutex);
            if (device->magma_system_device) {
                if (msd_device_display_get_size(device->magma_system_device->msd_dev(),
                                                value_out) == MAGMA_STATUS_OK) {
                    result = sizeof(*value_out);
                }
            }
            break;
        }

        case IOCTL_MAGMA_CONNECT: {
            DLOG("IOCTL_MAGMA_CONNECT");
            auto request = reinterpret_cast<const magma_system_connection_request*>(in_buf);
            if (!in_buf || in_len < sizeof(*request))
                return DRET(ZX_ERR_INVALID_ARGS);

            auto device_handle_out = reinterpret_cast<uint32_t*>(out_buf);
            if (!out_buf || out_len < sizeof(*device_handle_out) * 2)
                return DRET(ZX_ERR_INVALID_ARGS);

            if ((request->capabilities & MAGMA_CAPABILITY_RENDERING) ||
                (request->capabilities & MAGMA_CAPABILITY_DISPLAY) == 0)
                return DRET(ZX_ERR_INVALID_ARGS);

            reset_placeholder(device);
            magma_system_image_descriptor image_desc{MAGMA_IMAGE_TILING_OPTIMAL};
            device->magma_system_device->PageFlipAndEnable(device->placeholder_framebuffer,
                                                           &image_desc, true);
            device->console_visible = false;
            if (device->ownership_change_callback)
                device->ownership_change_callback(false, device->ownership_change_cookie);

            auto connection = MagmaSystemDevice::Open(device->magma_system_device,
                                                      request->client_id, MAGMA_CAPABILITY_DISPLAY);
            if (!connection)
                return DRET(ZX_ERR_INVALID_ARGS);

            device_handle_out[0] = connection->GetHandle();
            device_handle_out[1] = connection->GetNotificationChannel();
            *out_actual = sizeof(*device_handle_out) * 2;
            result = ZX_OK;

            device->magma_system_device->StartConnectionThread(std::move(connection));

            break;
        }

        default:
            DLOG("sysdrv_display_ioctl unhandled op 0x%x", op);
    }

    return result;
}

static void sysdrv_display_release(void* ctx)
{
    // TODO(ZX-1170) - when testable:
    // Perform magma_stop but don't free the context unless sysdrv_gpu_release has already been
    // called
    DASSERT(false);
}

static zx_protocol_device_t sysdrv_display_device_proto = {
    .version = DEVICE_OPS_VERSION, .ioctl = sysdrv_display_ioctl, .release = sysdrv_display_release,
};

static void sysdrv_gpu_release(void* ctx)
{
    // TODO(ZX-1170) - when testable:
    // Free context if sysdrv_display_release has already been called
    DASSERT(false);
}

static zx_protocol_device_t sysdrv_gpu_device_proto = {
    .version = DEVICE_OPS_VERSION, .ioctl = sysdrv_gpu_ioctl, .release = sysdrv_gpu_release,
};

// implement driver object:

static zx_status_t sysdrv_bind(void* ctx, zx_device_t* zx_device)
{
    DLOG("sysdrv_bind start zx_device %p", zx_device);

    // map resources and initialize the device
    auto device = std::make_unique<sysdrv_device_t>();

    device->core_device = MsdIntelDeviceCore::Create(zx_device);
    if (!device->core_device)
        return DRET_MSG(ZX_ERR_INTERNAL, "couldn't create core device");

    device->gpu_core_protocol.ops = &gpu_core_protocol_ops;
    device->gpu_core_protocol.ctx = device.get();

    zx_display_info_t* di = &device->info;
    uint32_t format, width, height, stride, pitch;
    zx_status_t status = zx_bootloader_fb_get_info(&format, &width, &height, &stride);
    if (status == ZX_OK) {
        di->format = format;
        di->width = width;
        di->height = height;
        di->stride = stride;
    } else {
        di->format = ZX_PIXEL_FORMAT_ARGB_8888;
        di->width = 2560 / 2;
        di->height = 1700 / 2;
        di->stride = 2560 / 2;
    }

    switch (di->format) {
        case ZX_PIXEL_FORMAT_RGB_565:
            pitch = di->stride * sizeof(uint16_t);
            break;
        default:
            DLOG("unrecognized format 0x%x, defaulting to 32bpp", di->format);
        case ZX_PIXEL_FORMAT_ARGB_8888:
        case ZX_PIXEL_FORMAT_RGB_x888:
            pitch = di->stride * sizeof(uint32_t);
            break;
    }

    device->framebuffer_size = pitch * di->height;

    device->console_buffer =
        magma::PlatformBuffer::Create(device->framebuffer_size, "console-buffer");

    if (!device->console_buffer->MapCpu(&device->framebuffer_addr))
        return DRET_MSG(ZX_ERR_NO_MEMORY, "Failed to map framebuffer");

    // Placeholder is in tiled format
    device->placeholder_buffer = magma::PlatformBuffer::Create(
        magma::round_up(pitch, 512) * di->height, "placeholder-buffer");

    di->flags = ZX_DISPLAY_FLAG_HW_FRAMEBUFFER;

    // Tell the kernel about the console framebuffer so it can display a kernel
    // panic screen.
    // If other display clients come along and change the scanout address, then
    // the panic
    // won't be visible; however the plan is to move away from onscreen panics,
    // instead
    // writing the log somewhere it can be recovered then triggering a reboot.
    uint32_t handle;
    if (!device->console_buffer->duplicate_handle(&handle))
        return DRET_MSG(ZX_ERR_INTERNAL, "Failed to duplicate framebuffer handle");

    status = zx_set_framebuffer_vmo(get_root_resource(), handle, device->framebuffer_size, format,
                                    width, height, stride);
    if (status != ZX_OK)
        magma::log(magma::LOG_WARNING, "Failed to pass framebuffer to zircon: %d", status);

    if (magma::PlatformTrace::Get())
        magma::PlatformTrace::Get()->Initialize();

    device->magma_driver = MagmaDriver::Create();
    if (!device->magma_driver)
        return DRET_MSG(ZX_ERR_INTERNAL, "MagmaDriver::Create failed");

#if MAGMA_TEST_DRIVER
    DLOG("running magma indriver test");
    {
        auto platform_device = MsdIntelPciDevice::CreateShim(&device->gpu_core_protocol);
        magma_indriver_test(platform_device.get(), device->core_device.get());
    }
#endif

    device->parent_device = zx_device;

    status = magma_start(device.get());
    if (status != ZX_OK)
        return DRET_MSG(status, "magma_start failed");

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "magma_display";
    args.ctx = device.get();
    args.ops = &sysdrv_display_device_proto;
    args.proto_id = ZX_PROTOCOL_DISPLAY;
    args.proto_ops = &sysdrv_display_proto;

    status = device_add(zx_device, &args, &device->zx_device_display);
    if (status != ZX_OK)
        return DRET_MSG(status, "display device_add failed: %d", status);

    args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "magma_gpu";
    args.ctx = device.get();
    args.ops = &sysdrv_gpu_device_proto;
    args.proto_id = ZX_PROTOCOL_GPU;
    args.proto_ops = nullptr;

    status = device_add(zx_device, &args, &device->zx_device_gpu);
    if (status != ZX_OK)
        return DRET_MSG(status, "gpu device_add failed: %d", status);

    device.release();

    DLOG("initialized magma system driver");

    return ZX_OK;
}

zx_driver_ops_t msd_driver_ops = {
    .version = DRIVER_OPS_VERSION, .bind = sysdrv_bind,
};

static int magma_start(sysdrv_device_t* device)
{
    DLOG("magma_start");

    device->magma_system_device = device->magma_driver->CreateDevice(&device->gpu_core_protocol);
    if (!device->magma_system_device)
        return DRET_MSG(ZX_ERR_NO_RESOURCES, "Failed to create device");

    DLOG("Created device %p", device->magma_system_device.get());

    auto present_buffer_callback =
        [device](std::shared_ptr<MagmaSystemBuffer> buf, magma_system_image_descriptor* image_desc,
                 uint32_t wait_semaphore_count, uint32_t signal_semaphore_count,
                 std::vector<std::shared_ptr<MagmaSystemSemaphore>> semaphores,
                 std::unique_ptr<magma::PlatformSemaphore> buffer_presented_semaphore) {
            std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores(
                wait_semaphore_count);
            uint32_t index = 0;
            for (uint32_t i = 0; i < wait_semaphore_count; i++) {
                wait_semaphores[i] =
                    MsdIntelAbiSemaphore::cast(semaphores[index++]->msd_semaphore())->ptr();
            }
            std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores(
                signal_semaphore_count);
            for (uint32_t i = 0; i < signal_semaphore_count; i++) {
                signal_semaphores[i] =
                    MsdIntelAbiSemaphore::cast(semaphores[index++]->msd_semaphore())->ptr();
            }

            // We can't use our buffer wrapper objects here because there's no thread safety.
            uint32_t buffer_handle;
            if (!buf->platform_buffer()->duplicate_handle(&buffer_handle)) {
                magma::log(magma::LOG_WARNING,
                           "Failed to duplicate handle; can't present this buffer");
                return;
            }

            void* data =
                buffer_presented_semaphore ? buffer_presented_semaphore.release() : nullptr;

            device->core_device->PresentBuffer(
                buffer_handle, image_desc, std::move(wait_semaphores), std::move(signal_semaphores),
                [data](magma_status_t status, uint64_t vblank_time_ns) {
                    if (status != MAGMA_STATUS_OK) {
                        DLOG("page_flip_callback: error status %d", status);
                        return;
                    }

                    if (data) {
                        auto semaphore = reinterpret_cast<magma::PlatformSemaphore*>(data);
                        semaphore->Signal();
                        delete semaphore;
                    }
                });
        };

    device->magma_system_device->SetPresentBufferCallback(present_buffer_callback);

    DASSERT(device->console_buffer);
    DASSERT(device->placeholder_buffer);

    uint32_t buffer_handle;

    if (!device->console_buffer->duplicate_handle(&buffer_handle))
        return DRET_MSG(ZX_ERR_NO_RESOURCES, "duplicate_handle failed");

    device->console_framebuffer =
        MagmaSystemBuffer::Create(magma::PlatformBuffer::Import(buffer_handle));
    if (!device->console_framebuffer)
        return DRET_MSG(ZX_ERR_NO_MEMORY, "failed to created magma system buffer");

    int result = reset_placeholder(device);
    if (result != 0)
        return result;

    magma_system_image_descriptor image_desc{MAGMA_IMAGE_TILING_LINEAR};
    device->magma_system_device->PageFlipAndEnable(device->console_framebuffer, &image_desc, false);

    return ZX_OK;
}

#if MAGMA_TEST_DRIVER
static int magma_stop(sysdrv_device_t* device)
{
    DLOG("magma_stop");

    device->console_framebuffer.reset();
    device->placeholder_framebuffer.reset();

    device->magma_system_device->Shutdown();
    device->magma_system_device.reset();

    // Release all client gtt allocations
    for (auto addr : device->client_gtt_allocations) {
        device->core_device->gtt()->Free(addr);
    }
    device->client_gtt_allocations.clear();
    device->client_mmio_allocations.clear();
    device->core_device->UnregisterCallback();

    return ZX_OK;
}
#endif


// clang-format off
ZIRCON_DRIVER_BEGIN(pci_gpu, msd_driver_ops, "zircon", "!0.1", 5)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, MAGMA_PCI_VENDOR_ID),
    BI_MATCH_IF(EQ, BIND_PCI_CLASS, 0x3), // Display class
ZIRCON_DRIVER_END(pci_gpu)
