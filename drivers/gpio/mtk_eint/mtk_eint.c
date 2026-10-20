// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/platform-defs.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <zircon/vmid.h>
#include <zircon/device/grt-eint.h>
#include "mt8668-eint-hw.h"
#include <ddk/protocol/eint.h>

#define MTK_EINT_EDGE_SENSITIVE 0
#define MTK_EINT_LEVEL_SENSITIVE 1
#define MTK_EINT_DBNC_SET_DBNC_BITS 4
#define MTK_EINT_DBNC_RST_BIT (0x1 << 1)
#define MTK_EINT_DBNC_SET_EN (0x1 << 0)

#define MTK_EINT_NO_OFFSET 0
#define MAX_PIN 256
#define MTK_GPIO_BASE 0x10005000
#define MTK_GPIO_MMIO_SIZE 0x1000
#define MTK_GPIO_DIN_OFFSET 0x200
#define MTK_GPIO_REG_STRIDE 0x10

struct mtk_eint_pin {
  uint8_t instance;
  uint8_t index;
  bool debounce;
  bool dual_edge;
  bool is_level;
};

struct mtk_eint_instance {
  const char *name;
  void *mmio;
  size_t mmio_size;
  zx_handle_t mmio_vmo;
  uint32_t number;
  uint16_t pin_list[MAX_PIN];
  uint32_t *wake_mask;
  uint32_t *cur_mask;
};

struct mtk_eint_irq_entry {
  uint32_t eint_num;
  uint8_t vmid;
  bool is_callback;
  zx_handle_t irq_handle;
  struct {
    eint_irq_handler_t handler;
    void *ctx;
  } callback;
};

typedef struct {
  zx_device_t *zxdev;
  zx_device_t *parent;

  platform_device_protocol_t pdev;
  platform_bus_protocol_t pbus;
  eint_protocol_t eint_proto;
  uint32_t irq;
  pdev_irq_t out_irq;
  thrd_t irq_thread;
  zx_handle_t irq_event;
  zx_handle_t irq_handle;
  zx_handle_t gpio_bti;
  io_buffer_t gpio_regs;
  void *gpio_base;

  const struct mtk_eint_regs *regs;
  const struct mtk_eint_hw_desc *hw;

  struct mtk_eint_pin *pins;
  struct mtk_eint_instance *instances;
  struct mtk_eint_irq_entry *irq_entries;
  size_t irq_entry_count;
  size_t irq_entry_capacity;
  uint32_t total_pin_number;
  uint32_t instance_number;
  uint32_t dump_target_eint;
} mtk_eint_t;

static inline uint32_t read_reg(volatile void *base, uint32_t offset) {
  return *(volatile uint32_t *)((uint8_t *)base + offset);
}

static inline void write_reg(volatile void *base, uint32_t offset,
                             uint32_t value) {
  *(volatile uint32_t *)((uint8_t *)base + offset) = value;
}

static volatile void *mtk_eint_get_offset(mtk_eint_t *eint, uint32_t eint_num,
                                          uint32_t offset, uint32_t *instance,
                                          uint32_t *index) {
  volatile void *reg;

  if (eint_num >= eint->total_pin_number) {
    zxlogf(ERROR, "Invalid eint_num %d\n", eint_num);
    return NULL;
  }

  *instance = eint->pins[eint_num].instance;
  *index = eint->pins[eint_num].index;
  reg = (uint8_t *)eint->instances[*instance].mmio + offset + (*index / 32 * 4);

  return reg;
}

static bool mtk_eint_is_valid_pin(mtk_eint_t *eint, uint32_t eint_num) {
  return eint_num < eint->total_pin_number;
}

static ssize_t mtk_eint_find_irq_entry(mtk_eint_t *eint, uint32_t eint_num) {
  size_t i;

  for (i = 0; i < eint->irq_entry_count; i++) {
    if (eint->irq_entries[i].eint_num == eint_num) {
      return (ssize_t)i;
    }
  }

  return -1;
}

/*
 * Dynamic alloc memory for IRQ entries.
 */
static zx_status_t mtk_eint_ensure_irq_capacity(mtk_eint_t *eint) {
  struct mtk_eint_irq_entry *new_entries;
  size_t new_capacity;

  if (eint->irq_entry_count < eint->irq_entry_capacity) {
    return ZX_OK;
  }

  new_capacity = eint->irq_entry_capacity ? eint->irq_entry_capacity * 2 : 8;
  new_entries = realloc(eint->irq_entries,
                        new_capacity * sizeof(*eint->irq_entries));
  if (!new_entries) {
    return ZX_ERR_NO_MEMORY;
  }

  for (size_t i = eint->irq_entry_capacity; i < new_capacity; i++) {
    new_entries[i].irq_handle = ZX_HANDLE_INVALID;
    new_entries[i].is_callback = false;
  }

  eint->irq_entries = new_entries;
  eint->irq_entry_capacity = new_capacity;
  return ZX_OK;
}

static void mtk_eint_remove_irq_entry(mtk_eint_t *eint, size_t entry_idx) {
  size_t last_idx;

  if (entry_idx >= eint->irq_entry_count) {
    return;
  }

  if (eint->irq_entries[entry_idx].irq_handle != ZX_HANDLE_INVALID) {
    zx_handle_close(eint->irq_entries[entry_idx].irq_handle);
  }

  last_idx = eint->irq_entry_count - 1;
  if (entry_idx != last_idx) {
    eint->irq_entries[entry_idx] = eint->irq_entries[last_idx];
  }

  memset(&eint->irq_entries[last_idx], 0, sizeof(eint->irq_entries[last_idx]));
  eint->irq_entries[last_idx].irq_handle = ZX_HANDLE_INVALID;
  eint->irq_entry_count--;
}

static zx_status_t mtk_eint_init_gpio_regs(mtk_eint_t *eint) {
  zx_status_t status = pdev_get_bti(&eint->pdev, 0, &eint->gpio_bti);
  if (status != ZX_OK) {
    zxlogf(ERROR, "mtk-eint: failed to get BTI for GPIO map: %s\n",
           zx_status_get_string(status));
    return status;
  }

  status = io_buffer_init_physical(&eint->gpio_regs, eint->gpio_bti,
                                   MTK_GPIO_BASE, MTK_GPIO_MMIO_SIZE,
                                   get_root_resource(),
                                   ZX_CACHE_POLICY_UNCACHED_DEVICE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "mtk-eint: failed to map GPIO regs: %s\n",
           zx_status_get_string(status));
    zx_handle_close(eint->gpio_bti);
    eint->gpio_bti = ZX_HANDLE_INVALID;
    return status;
  }

  eint->gpio_base = io_buffer_virt(&eint->gpio_regs);
  return ZX_OK;
}

static zx_status_t mtk_eint_get_gpio_level(mtk_eint_t *eint, uint32_t eint_num,
                                           uint32_t *out_level) {
  uint32_t pos;
  uint32_t bit;
  uint32_t reg;

  if (!out_level) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (!eint->gpio_base) {
    return ZX_ERR_BAD_STATE;
  }
  if (!mtk_eint_is_valid_pin(eint, eint_num)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  /*
   * On MT8668 the EINT number matches the GPIO pin number, so a local DIN read
   * is enough for dual-edge emulation without depending on another driver.
   */
  pos = eint_num / 32;
  bit = eint_num % 32;
  reg = read_reg((uint8_t *)eint->gpio_base + MTK_GPIO_DIN_OFFSET,
                 pos * MTK_GPIO_REG_STRIDE);
  *out_level = !!(reg & (1u << bit));
  return ZX_OK;
}

static uint32_t mtk_eint_flip_edge(mtk_eint_t *eint, uint32_t eint_num) {
  uint32_t start_level = 0;
  uint32_t curr_level = 0;
  uint32_t reg_offset;
  uint32_t instance;
  uint32_t index;
  uint32_t mask;
  uint32_t port;
  volatile void *reg;

  reg = mtk_eint_get_offset(eint, eint_num, MTK_EINT_NO_OFFSET, &instance,
                            &index);
  if (!reg) {
    return 0;
  }

  mask = 1u << (index & 0x1f);
  port = index >> 5;

  if (mtk_eint_get_gpio_level(eint, eint_num, &curr_level) != ZX_OK) {
    return 0;
  }

  do {
    start_level = curr_level;
    reg_offset = start_level ? eint->regs->pol_clr : eint->regs->pol_set;
    write_reg(eint->instances[instance].mmio, port * 4 + reg_offset, mask);
  } while (mtk_eint_get_gpio_level(eint, eint_num, &curr_level) == ZX_OK &&
           start_level != curr_level);

  return start_level;
}

static void mtk_eint_mask(mtk_eint_t *eint, uint32_t eint_num) {
  uint32_t instance, index;
  volatile void *reg = mtk_eint_get_offset(eint, eint_num, eint->regs->mask_set,
                                           &instance, &index);
  uint32_t mask = 1 << (index & 0x1f);

  if (!reg) {
    zxlogf(ERROR, "%s invalid eint_num %d\n", __func__, eint_num);
    return;
  }

  eint->instances[instance].cur_mask[index >> 5] &= ~mask;
  write_reg(reg, 0, mask);
}

static void mtk_eint_unmask(mtk_eint_t *eint, uint32_t eint_num) {
  uint32_t instance, index;
  volatile void *reg = mtk_eint_get_offset(eint, eint_num, eint->regs->mask_clr,
                                           &instance, &index);
  uint32_t mask = 1 << (index & 0x1f);

  if (!reg) {
    zxlogf(ERROR, "%s invalid eint_num %d\n", __func__, eint_num);
    return;
  }

  eint->instances[instance].cur_mask[index >> 5] |= mask;
  write_reg(reg, 0, mask);

  if (eint->pins[eint_num].dual_edge) {
    mtk_eint_flip_edge(eint, eint_num);
  }
}

static void mtk_eint_ack(mtk_eint_t *eint, uint32_t eint_num) {
  uint32_t instance, index;
  volatile void *reg =
      mtk_eint_get_offset(eint, eint_num, eint->regs->ack, &instance, &index);
  uint32_t bit = 1 << (index & 0x1f);

  if (!reg) {
    zxlogf(ERROR, "%s invalid eint_num %d\n", __func__, eint_num);
    return;
  }

  write_reg(reg, 0, bit);
}

static zx_status_t mtk_eint_soft_write(mtk_eint_t *eint, uint32_t eint_num,
                                       uint32_t offset) {
  uint32_t instance, index;
  volatile void *reg =
      mtk_eint_get_offset(eint, eint_num, offset, &instance, &index);
  uint32_t bit = 1u << (index & 0x1f);

  if (!reg) {
    zxlogf(ERROR, "%s invalid eint_num %d\n", __func__, eint_num);
    return ZX_ERR_OUT_OF_RANGE;
  }

  write_reg(reg, 0, bit);
  return ZX_OK;
}
#if 0
static uint32_t mtk_eint_get_debounce_en(mtk_eint_t *eint, uint32_t eint_num) {
	uint32_t instance, index, bit;
	volatile void *reg;

	reg = mtk_eint_get_offset(eint, eint_num, MTK_EINT_NO_OFFSET,
				  &instance, &index);

	if (!reg) {
		zxlogf(ERROR, "%s invalid eint_num %d\n", __func__, eint_num);
		return 0;
	}

	reg = (uint8_t *)eint->instances[instance].mmio +
		(index / 4) * 4 + eint->regs->dbnc_ctrl;

	bit = MTK_EINT_DBNC_SET_EN << ((index % 4) * 8);

	return (read_reg(reg, 0) & bit) ? 1 : 0;
}

static int mtk_eint_set_debounce(mtk_eint_t *eint, uint32_t eint_num,
				  uint32_t debounce) {
	uint32_t set_offset, bit, clr_bit, clr_offset, rst, dbnc;
	static const uint32_t debounce_time[] = { 156, 313, 625, 1250,
		20000, 40000, 80000, 160000, 320000, 640000 };
	uint32_t instance, index;
	volatile void *reg;
	uint32_t i;

	reg = mtk_eint_get_offset(eint, eint_num, MTK_EINT_NO_OFFSET,
				  &instance, &index);

	if (!reg) {
		zxlogf(ERROR, "%s invalid eint_num %d\n", __func__, eint_num);
		return ZX_ERR_INVALID_ARGS;
	}

	dbnc = sizeof(debounce_time) / sizeof(debounce_time[0]) - 1;
	for (i = 0; i < sizeof(debounce_time) / sizeof(debounce_time[0]); i++) {
		if (debounce <= debounce_time[i]) {
			dbnc = i;
			break;
		}
	}

	reg = eint->instances[instance].mmio;
	set_offset = (index / 4) * 4 + eint->regs->dbnc_set;
	clr_offset = (index / 4) * 4 + eint->regs->dbnc_clr;

	clr_bit = 0xff << ((index % 4) * 8);
	write_reg(reg, clr_offset, clr_bit);

	bit = ((dbnc << MTK_EINT_DBNC_SET_DBNC_BITS)
		| MTK_EINT_DBNC_SET_EN) << ((index % 4) * 8);
	rst = MTK_EINT_DBNC_RST_BIT << ((index % 4) * 8);
	write_reg(reg, set_offset, rst | bit);

	zx_nanosleep(zx_deadline_after(ZX_USEC(250)));

	return ZX_OK;
}
#endif

static void eint_irq_handler(void *arg) {
  mtk_eint_t *eint = arg;
  uint32_t status, i, j;
  int shift, port, eint_num;
  uint32_t dual_edge, start_level, curr_level;
  uint32_t instance, index;
  volatile void *addr, *reg;

  for (i = 0; i < eint->instance_number; i++) {
    for (j = 0; j < eint->instances[i].number; j += 32) {
      port = j >> 5;
      status = read_reg(eint->instances[i].mmio, port * 4 + eint->regs->stat);
      while (status) {
        shift = __builtin_ctz(status);
        status &= ~(1 << shift);

        eint_num = eint->instances[i].pin_list[shift + j];
        dual_edge = eint->pins[eint_num].dual_edge;

        if (eint->instances[i].wake_mask[port] & (1 << shift) &&
            !(eint->instances[i].cur_mask[port] & (1 << shift))) {
          addr = (uint8_t *)eint->instances[i].mmio + port * 4 +
                 eint->regs->mask_set;
          write_reg(addr, 0, 1 << shift);
        }

        if (dual_edge &&
            mtk_eint_get_gpio_level(eint, eint_num, &start_level) != ZX_OK) {
          start_level = 0;
        }

        mtk_eint_mask(eint, eint_num);
        mtk_eint_ack(eint, eint_num);
        ssize_t irq_idx = mtk_eint_find_irq_entry(eint, eint_num);
        if (irq_idx >= 0) {
          struct mtk_eint_irq_entry *entry = &eint->irq_entries[irq_idx];
          uint32_t is_forward = 1;
          if (entry->is_callback) {
            if (entry->callback.handler != NULL) {
              entry->callback.handler(entry->callback.ctx, eint_num, &is_forward);
            } else {
              zxlogf(ERROR, "mtk-eint: callback handler is NULL for eint %u\n", eint_num);
            }
          }
          if (is_forward && entry->irq_handle != ZX_HANDLE_INVALID) {
            zx_status_t status =
                zx_object_signal(entry->irq_handle, 0, ZX_USER_SIGNAL_0);
            if (status != ZX_OK) {
              zxlogf(ERROR, "mtk-eint: failed to signal virt irq %u: %d\n",
                     eint_num, status);
            }
          }
        }
        mtk_eint_unmask(eint, eint_num);
        if (dual_edge) {
          curr_level = mtk_eint_flip_edge(eint, eint_num);
          if (start_level != curr_level) {
            (void)mtk_eint_soft_write(eint, eint_num, eint->regs->soft_set);
          }
          (void)mtk_eint_soft_write(eint, eint_num, eint->regs->soft_clr);
        }

        if (eint->pins[eint_num].debounce) {
          uint32_t rst, ctrl_offset, bit, dbnc;
          reg = mtk_eint_get_offset(eint, eint_num, MTK_EINT_NO_OFFSET,
                                    &instance, &index);
          if (!reg)
            continue;

          ctrl_offset = (index / 4) * 4 + eint->regs->dbnc_ctrl;
          dbnc = read_reg(eint->instances[instance].mmio, ctrl_offset);
          bit = MTK_EINT_DBNC_SET_EN << ((index % 4) * 8);

          if ((bit & dbnc) > 0) {
            ctrl_offset = (index / 4) * 4 + eint->regs->dbnc_set;
            rst = MTK_EINT_DBNC_RST_BIT << ((index % 4) * 8);
            write_reg(eint->instances[instance].mmio, ctrl_offset, rst);
          }
        }
      }
    }
  }
}

static zx_status_t mtk_eint_set_type(mtk_eint_t *eint, uint32_t eint_num,
                                     uint32_t type) {
  uint32_t instance, index;
  volatile void *reg;
  uint32_t mask;

  bool pol_high = ((type & (GPIO_TRIGGER_HIGH | GPIO_TRIGGER_RISING)) != 0);
  reg = mtk_eint_get_offset(
      eint, eint_num, pol_high ? eint->regs->pol_set : eint->regs->pol_clr,
      &instance, &index);
  if (!reg)
    return ZX_ERR_INVALID_ARGS;
  mask = (1u << (index & 0x1f));
  write_reg(reg, 0, mask);

  bool sens_level = ((type & GPIO_TRIGGER_LEVEL) == GPIO_TRIGGER_LEVEL);
  eint->pins[eint_num].is_level = sens_level;
  reg = mtk_eint_get_offset(
      eint, eint_num, sens_level ? eint->regs->sens_set : eint->regs->sens_clr,
      &instance, &index);
  if (!reg)
    return ZX_ERR_INVALID_ARGS;
  write_reg(reg, 0, mask);

  if (type & GPIO_TRIGGER_RISING && type & GPIO_TRIGGER_FALLING) {
    eint->pins[eint_num].dual_edge = true;
    mtk_eint_flip_edge(eint, eint_num);
  } else {
    eint->pins[eint_num].dual_edge = false;
  }

  return ZX_OK;
}

static zx_status_t mtk_eint_request(void *ctx, uint32_t eint_num,
                                uint8_t vmid, zx_handle_t *out_irq) {
  mtk_eint_t *eint = ctx;
  zx_status_t status;
  zx_handle_t irq_handle = ZX_HANDLE_INVALID;
  uint32_t inst, idx;
  ssize_t entry_idx;

  if (!mtk_eint_is_valid_pin(eint, eint_num))
    return ZX_ERR_OUT_OF_RANGE;

  entry_idx = mtk_eint_find_irq_entry(eint, eint_num);
  if (entry_idx >= 0 &&
      eint->irq_entries[entry_idx].irq_handle != ZX_HANDLE_INVALID) {
    zxlogf(ERROR, "mtk-eint: EINT %u already registered\n", eint_num);
    return ZX_ERR_ALREADY_EXISTS;
  }

  status = mtk_eint_ensure_irq_capacity(eint);
  if (status != ZX_OK) {
    return status;
  }

  status = zx_event_create(0, &irq_handle);
  if (status != ZX_OK) {
    return status;
  }

  status = zx_handle_duplicate(irq_handle, ZX_RIGHT_SAME_RIGHTS, out_irq);
  if (status != ZX_OK) {
    zx_handle_close(irq_handle);
    return status;
  }

  if (entry_idx >= 0) {
    eint->irq_entries[entry_idx].irq_handle = irq_handle;
    eint->irq_entries[entry_idx].vmid = vmid;
    zxlogf(INFO, "mtk-eint: EINT %u shared eint pin, vmid: %d\n",
           eint_num, (int)ZX_VMID_USER_TO_GUEST(vmid));
  } else {
    eint->irq_entries[eint->irq_entry_count].eint_num = eint_num;
    eint->irq_entries[eint->irq_entry_count].vmid = vmid;
    eint->irq_entries[eint->irq_entry_count].is_callback = false;
    eint->irq_entries[eint->irq_entry_count].irq_handle = irq_handle;
    eint->irq_entries[eint->irq_entry_count].callback.handler = NULL;
    eint->irq_entries[eint->irq_entry_count].callback.ctx = NULL;
    eint->irq_entry_count++;
    zxlogf(INFO, "mtk-eint: EINT %u requested, vmid: %d\n",
           eint_num, (int)ZX_VMID_USER_TO_GUEST(vmid));
  }

  if (entry_idx < 0 || !eint->irq_entries[entry_idx].is_callback) {
    volatile void *reg =
        mtk_eint_get_offset(eint, eint_num, eint->regs->mask_set, &inst, &idx);
    write_reg(reg, 0, (1u << (idx & 0x1f)));
  }

  return ZX_OK;
}

static zx_status_t mtk_eint_vm_destroy(mtk_eint_t *eint, uint8_t vmid) {

  size_t i = 0;
  size_t entry_max = eint->irq_entry_count;

  while (i < entry_max) {
    if (eint->irq_entries[i].vmid == vmid) {
      mtk_eint_remove_irq_entry(eint, i);
      continue;
    }
    i++;
  }
  zxlogf(INFO, "mtk-eint: VM %d destroyed, removed related IRQ entries\n", vmid);

  return ZX_OK;
}

static zx_status_t mtk_eint_handle_simple_op(mtk_eint_t *eint,
                                             const pdev_eint_op_t *req,
                                             uint32_t op) {
  if (!req) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (!mtk_eint_is_valid_pin(eint, req->eint_num)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  switch (op) {
  case IOCTL_GRT_EINT_MASK:
    mtk_eint_mask(eint, req->eint_num);
    return ZX_OK;
  case IOCTL_GRT_EINT_UNMASK:
    mtk_eint_unmask(eint, req->eint_num);
    return ZX_OK;
  case IOCTL_GRT_EINT_ACK:
    mtk_eint_ack(eint, req->eint_num);
    return ZX_OK;
  default:
    return ZX_ERR_NOT_SUPPORTED;
  }
}

static zx_status_t mtk_eint_ioctl(void *ctx, uint32_t op, const void *in_buf,
                                  size_t in_len, void *out_buf, size_t out_len,
                                  size_t *out_actual) {
  mtk_eint_t *eint = ctx;

  switch (op) {
  case IOCTL_GRT_EINT_IRQ_REQUEST: {
    if (in_len < sizeof(uint32_t) || out_len < sizeof(zx_handle_t)) {
      return ZX_ERR_INVALID_ARGS;
    }
    const pdev_eint_req_t *req = (const pdev_eint_req_t *)in_buf;
    zx_status_t status =
        mtk_eint_request(eint, req->eint_num, req->vmid, (zx_handle_t *)out_buf);
    if (status != ZX_OK) {
      zxlogf(ERROR, "mtk-eint: failed to request EINT %u: %s\n", req->eint_num,
             zx_status_get_string(status));
      return status;
    }
    *out_actual = sizeof(zx_handle_t);
    return ZX_OK;
  }
  case IOCTL_GRT_EINT_SET_TYPE: {
    if (in_len < sizeof(pdev_eint_set_type_t)) {
      return ZX_ERR_INVALID_ARGS;
    }
    const pdev_eint_set_type_t *req = (const pdev_eint_set_type_t *)in_buf;
    return mtk_eint_set_type(eint, req->eint_num, req->type);
  }
  case IOCTL_GRT_EINT_MASK:
  case IOCTL_GRT_EINT_UNMASK:
  case IOCTL_GRT_EINT_ACK: {
    if (in_len < sizeof(pdev_eint_op_t)) {
      return ZX_ERR_INVALID_ARGS;
    }
    return mtk_eint_handle_simple_op(eint, (const pdev_eint_op_t *)in_buf, op);
  }
  case IOCTL_GRT_EINT_GET_MAX_EINT_NUM: {
    if (out_len < sizeof(uint32_t) || out_buf == NULL) {
      return ZX_ERR_INVALID_ARGS;
    }
    uint32_t val = eint->total_pin_number;
    memcpy(out_buf, &val, sizeof(uint32_t));
    if (out_actual)
      *out_actual = sizeof(uint32_t);
    return ZX_OK;
  }
  case IOCTL_GRT_EINT_VM_DESTROY: {
    if (in_len < sizeof(uint8_t)) {
      return ZX_ERR_INVALID_ARGS;
    }
    const uint8_t vmid = *(const uint8_t *)in_buf;
    return mtk_eint_vm_destroy(eint, vmid);
  }
  default:
    return ZX_ERR_NOT_SUPPORTED;
  }
}

static void mtk_eint_unbind(void *ctx) {
  mtk_eint_t *eint = ctx;
  device_remove(eint->zxdev);
}

static void mtk_eint_release(void *ctx) {
  mtk_eint_t *eint = ctx;
  uint32_t i;
  size_t irq_idx;

  zxlogf(INFO, "mtk-eint: releasing device\n");
  for (irq_idx = 0; irq_idx < eint->irq_entry_count; irq_idx++) {
    if (eint->irq_entries[irq_idx].irq_handle != ZX_HANDLE_INVALID) {
      zx_handle_close(eint->irq_entries[irq_idx].irq_handle);
      eint->irq_entries[irq_idx].irq_handle = ZX_HANDLE_INVALID;
    }
  }

  for (i = 0; i < eint->instance_number; i++) {
    struct mtk_eint_instance *inst = &eint->instances[i];

#if 0
        if (inst->mmio != NULL && inst->mmio_size > 0) {
            zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)inst->mmio, inst->mmio_size);
            inst->mmio = NULL;
        }
#endif

    if (inst->mmio_vmo != ZX_HANDLE_INVALID) {
      zx_handle_close(inst->mmio_vmo);
      inst->mmio_vmo = ZX_HANDLE_INVALID;
    }
    free(inst->wake_mask);
    free(inst->cur_mask);
  }
  if (eint->gpio_base) {
    io_buffer_release(&eint->gpio_regs);
    eint->gpio_base = NULL;
  }
  if (eint->gpio_bti != ZX_HANDLE_INVALID) {
    zx_handle_close(eint->gpio_bti);
    eint->gpio_bti = ZX_HANDLE_INVALID;
  }
  free(eint->irq_entries);
  free(eint->instances);
  free(eint->pins);
  free(eint);
}

static zx_status_t mtk_eint_proto_register_irq(void *ctx, uint32_t eint_num,
                                               uint32_t type,
                                               eint_irq_handler_t handler,
                                               void *handler_ctx) {
  mtk_eint_t *eint = ctx;
  zx_status_t status;
  ssize_t entry_idx;

  if (!handler) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (!mtk_eint_is_valid_pin(eint, eint_num)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  entry_idx = mtk_eint_find_irq_entry(eint, eint_num);
  if (entry_idx >= 0 && eint->irq_entries[entry_idx].is_callback) {
    zxlogf(ERROR, "mtk-eint: EINT %u already registered!\n", eint_num);
    return ZX_ERR_ALREADY_EXISTS;
  }

  status = mtk_eint_set_type(eint, eint_num, type);
  if (status != ZX_OK) {
    return status;
  }
  status = mtk_eint_ensure_irq_capacity(eint);
  if (status != ZX_OK) {
    return status;
  }

  if (entry_idx >= 0) {
    eint->irq_entries[entry_idx].is_callback = true;
    eint->irq_entries[entry_idx].callback.handler = handler;
    eint->irq_entries[entry_idx].callback.ctx = handler_ctx;
    zxlogf(INFO, "mtk-eint: register shared eint pin %u.\n", eint_num);
  } else {
    eint->irq_entries[eint->irq_entry_count].eint_num = eint_num;
    eint->irq_entries[eint->irq_entry_count].is_callback = true;
    eint->irq_entries[eint->irq_entry_count].irq_handle = ZX_HANDLE_INVALID;
    eint->irq_entries[eint->irq_entry_count].callback.handler = handler;
    eint->irq_entries[eint->irq_entry_count].callback.ctx = handler_ctx;
    eint->irq_entry_count++;

    mtk_eint_ack(eint, eint_num);
    mtk_eint_unmask(eint, eint_num);
  }

  return ZX_OK;
}

static zx_status_t mtk_eint_proto_unregister_irq(void *ctx, uint32_t eint_num) {
  mtk_eint_t *eint = ctx;
  ssize_t entry_idx;

  if (!mtk_eint_is_valid_pin(eint, eint_num)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  entry_idx = mtk_eint_find_irq_entry(eint, eint_num);
  if (entry_idx < 0) {
    return ZX_ERR_NOT_FOUND;
  }
  if (!eint->irq_entries[entry_idx].is_callback) {
    return ZX_ERR_BAD_STATE;
  }

  mtk_eint_mask(eint, eint_num);
  mtk_eint_ack(eint, eint_num);
  mtk_eint_remove_irq_entry(eint, (size_t)entry_idx);
  return ZX_OK;
}

static eint_protocol_ops_t mtk_eint_protocol_ops = {
    .register_irq = mtk_eint_proto_register_irq,
    .unregister_irq = mtk_eint_proto_unregister_irq,
};

static zx_status_t mtk_eint_get_protocol(void *ctx, uint32_t proto_id,
                                         void *protocol) {
  mtk_eint_t *eint = ctx;

  if (!protocol) {
    return ZX_ERR_INVALID_ARGS;
  }

  switch (proto_id) {
  case ZX_PROTOCOL_EINT: {
    eint_protocol_t *proto = protocol;
    proto->ops = &mtk_eint_protocol_ops;
    proto->ctx = eint;
    return ZX_OK;
  }
  default:
    return ZX_ERR_NOT_SUPPORTED;
  }
}

static zx_protocol_device_t mtk_eint_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = mtk_eint_get_protocol,
    .ioctl = mtk_eint_ioctl,
    .unbind = mtk_eint_unbind,
    .release = mtk_eint_release,
};

static zx_status_t mtk_eint_init_hw(mtk_eint_t *eint) {
  uint32_t i;
  zx_status_t status;

  eint->total_pin_number = eint->hw->total_pins;
  eint->instance_number = eint->hw->instance_count;

  zxlogf(INFO,
         "mtk_eint: Initializing %s configuration (%d instances, %d pins)\n",
         eint->hw->name, eint->instance_number, eint->total_pin_number);

  size_t size = eint->instance_number * sizeof(struct mtk_eint_instance);
  eint->instances = calloc(1, size);
  if (!eint->instances) {
    return ZX_ERR_NO_MEMORY;
  }

  size = eint->total_pin_number * sizeof(struct mtk_eint_pin);
  eint->pins = calloc(1, size);
  if (!eint->pins) {
    free(eint->instances);
    return ZX_ERR_NO_MEMORY;
  }

  // Map MMIO for all 4 instances
  for (i = 0; i < eint->instance_number; i++) {
    status =
        pdev_map_mmio(&eint->pdev, i, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                      &eint->instances[i].mmio, &eint->instances[i].mmio_size,
                      &eint->instances[i].mmio_vmo);
    if (status != ZX_OK) {
      zxlogf(ERROR, "mtk_eint: Failed to map MMIO for instance %d: %s\n", i,
             zx_status_get_string(status));
      return status;
    }
    eint->instances[i].name = eint->hw->instance_names[i];
    eint->instances[i].number = 0;
  }

  // Initialize pins from platform description
  for (i = 0; i < eint->hw->pin_count; i++) {
    uint32_t pin_id = eint->hw->pins[i].pin;
    uint32_t inst = eint->hw->pins[i].instance;
    uint32_t idx = eint->hw->pins[i].index;
    uint32_t debounce = eint->hw->pins[i].debounce;

    eint->pins[pin_id].instance = inst;
    eint->pins[pin_id].index = idx;
    eint->pins[pin_id].debounce = debounce;
    eint->instances[inst].pin_list[idx] = pin_id;

    if (idx + 1 > eint->instances[inst].number) {
      eint->instances[inst].number = idx + 1;
    }
  }

  // Allocate wake_mask and cur_mask for each instance
  for (i = 0; i < eint->instance_number; i++) {
    size = (eint->instances[i].number / 32 + 1) * sizeof(uint32_t);
    eint->instances[i].wake_mask = calloc(1, size);
    eint->instances[i].cur_mask = calloc(1, size);

    if (!eint->instances[i].wake_mask || !eint->instances[i].cur_mask) {
      return ZX_ERR_NO_MEMORY;
    }
  }

  // Initialize domain enable for all instances
  for (i = 0; i < eint->instance_number; i++) {
    uint32_t j;
    void *instance_mmio = eint->instances[i].mmio;
    for (j = 0; j < eint->instances[i].number; j += 32) {
      uint32_t offset = eint->regs->dom_en + (j / 32 * 4);
      write_reg(instance_mmio, offset, 0xffffffff);
    }
  }

  return ZX_OK;
}

static int eint_irq_thread(void *arg) {
  mtk_eint_t *eint = arg;
  zx_handle_t irq_handle = eint->irq_handle;

  while (1) {
    uint64_t slots;
    zx_status_t status = zx_interrupt_wait(irq_handle, &slots);
    if (status != ZX_OK) {
      zxlogf(INFO, "mtk-eint: IRQ thread exiting: %s\n",
             zx_status_get_string(status));
      break;
    }
    eint_irq_handler(eint);
  }
  return 0;
}

static zx_status_t eint_irq_init(mtk_eint_t *eint) {
  zx_status_t status;

  status = pdev_map_interrupt(&eint->pdev, 0, &eint->irq_handle);
  if (status != ZX_OK) {
    zxlogf(ERROR, "mtk-eint: pdev_map_interrupt failed\n");
    return status;
  }

  pdev_get_irq_info(&eint->pdev, 0, &eint->out_irq);
  eint->irq = eint->out_irq.irq;
  zxlogf(INFO, "mtk-eint: IRQ %u\n", eint->irq);

  status = thrd_create_with_name(&eint->irq_thread, eint_irq_thread, eint,
                                 "eint-irq");
  if (status != thrd_success) {
    zxlogf(ERROR, "eint: failed to create irq thread\n");
    zx_handle_close(eint->irq_handle);
    return status;
  }
  thrd_detach(eint->irq_thread);
  zx_interrupt_unmask(eint->irq_handle, eint->irq);

  return ZX_OK;
}

static zx_status_t mtk_eint_bind(void *ctx, zx_device_t *device) {
  zxlogf(INFO, "mtk_eint_bind: starting\n");
  bool device_added = false;

  mtk_eint_t *eint = calloc(1, sizeof(mtk_eint_t));
  if (!eint) {
    zxlogf(ERROR, "mtk_eint_bind: not enough memory\n");
    return ZX_ERR_NO_MEMORY;
  }

  eint->parent = device;
  eint->hw = &mt8668_eint_hw;
  eint->regs = eint->hw->regs;

  zx_status_t status =
      device_get_protocol(device, ZX_PROTOCOL_PLATFORM_DEV, &eint->pdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "mtk_eint_bind: failed to get pdev protocol: %s\n",
           zx_status_get_string(status));
    goto err_out;
  }

  status = device_get_protocol(device, ZX_PROTOCOL_PLATFORM_BUS, &eint->pbus);
  if (status != ZX_OK) {
    zxlogf(ERROR, "mtk_eint_bind: failed to get pbus protocol: %s\n",
           zx_status_get_string(status));
    goto err_out;
  }

  status = mtk_eint_init_hw(eint);
  if (status != ZX_OK) {
    zxlogf(INFO, "mtk-eint: Failed to init %s config\n", eint->hw->name);
    goto err_out;
  }

  status = mtk_eint_init_gpio_regs(eint);
  if (status != ZX_OK) {
    goto err_out;
  }

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "mtk_eint",
      .ctx = eint,
      .ops = &mtk_eint_device_proto,
      .flags = DEVICE_ADD_MUST_ISOLATE,
  };

  status = device_add(device, &args, &eint->zxdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "mtk_eint_bind: device_add failed: %s\n",
           zx_status_get_string(status));
    goto err_out;
  }
  device_added = true;

  status = eint_irq_init(eint);
  if (status != ZX_OK) {
    zxlogf(ERROR, "mtk_eint_bind: eint_irq_init failed: %s\n",
           zx_status_get_string(status));
    goto err_out;
  }

  eint->eint_proto.ops = &mtk_eint_protocol_ops;
  eint->eint_proto.ctx = eint;
  status = pbus_set_protocol(&eint->pbus, ZX_PROTOCOL_EINT, &eint->eint_proto);
  if (status != ZX_OK) {
    zxlogf(ERROR, "mtk_eint_bind: pbus_set_protocol(EINT) failed: %s\n",
           zx_status_get_string(status));
    goto err_out;
  }

  zxlogf(INFO, "mtk_eint_bind: successfully bound with %s configuration\n",
         eint->hw->name);
  return ZX_OK;

err_out:
  if (device_added) {
    device_remove(eint->zxdev);
  } else {
    mtk_eint_release(eint);
  }
  return status;
}

static zx_driver_ops_t mtk_eint_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = mtk_eint_bind,
};

ZIRCON_DRIVER_BEGIN(mtk_eint, mtk_eint_driver_ops, "mtk_eint", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MTK),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_MT8668),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_GRT_EINT),
ZIRCON_DRIVER_END(mtk_eint)
