/* SPDX-License-Identifier: GPL-2.0 */

#include "garnet/lib/linux/platform_device.h"

#include <binding.h>
#include <ddk/debug.h>
#include <linux/platform_device.h>
#include <zircon/types.h>

__WEAK platform_probe_fn_t __platform_device_probe;
__WEAK struct platform_device_id* __dev_table_platform;

#define DEFINE_LOG_FUNC(severity)        \
  char buf[1024];                        \
  va_list args;                          \
  va_start(args, fmt);                   \
  vsprintf(buf, fmt, args);              \
  va_end(args);                          \
  size_t len = strlen(buf);              \
  if (len > 0 && buf[len - 1] == '\n') { \
    buf[len - 1] = '\0';                 \
  }                                      \
  LOG(severity) << buf;

void dev_dbg(const struct device* dev, const char* fmt, ...) {
  DEFINE_LOG_FUNC(DEBUG);
}
void dev_err(const struct device* dev, const char* fmt, ...) {
  DEFINE_LOG_FUNC(ERROR);
}
void dev_info(const struct device* dev, const char* fmt, ...) {
  DEFINE_LOG_FUNC(INFO);
}

// static
zx_status_t PlatformDeviceContext::Create(zx_device_t* parent) {
  if (!__platform_device_probe)
    return ZX_ERR_NOT_FOUND;

  auto ctx = PlatformDeviceContext::Instance();
  if (!ctx)
    return ZX_ERR_NO_MEMORY;

  ctx->Init(parent);

  auto pdev = ctx->get_linux_pdev();
  pdev->name = device_get_name(parent);
  if (__dev_table_platform) {
    for (struct platform_device_id* id = __dev_table_platform; id->id; id++) {
      if (id->id == ctx->id()) {
        pdev->id_entry = id;
      }
    }
  }

  int ret = __platform_device_probe(pdev);
  if (ret < 0) {
    LOG(ERROR) << "Platform device probe failed with error: " << ret;
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

struct resource* platform_get_resource(struct platform_device* pdev,
                                       unsigned int type,
                                       unsigned int num) {
  auto ctx = PlatformDeviceContext::Instance();
  return ctx->get_resource(num);
}

int platform_get_irq(struct platform_device* pdev, unsigned int num) {
  auto ctx = PlatformDeviceContext::Instance();
  return ctx->get_irq_handle(num);
}

struct platform_device_id* platform_get_device_id(
    struct platform_device* pdev) {
  return const_cast<platform_device_id*>(pdev->id_entry);
}

void* platform_get_drvdata(const struct platform_device* pdev) {
  return pdev->dev.platform_data;
}

void platform_set_drvdata(struct platform_device* pdev, void* data) {
  pdev->dev.platform_data = data;
}