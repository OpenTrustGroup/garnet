// SPDX-License-Identifier: BSD-3-Clause
#pragma once

__BEGIN_CDECLS;

#include <stddef.h>
#include <stdint.h>

struct mtk_eint_regs {
  uint32_t stat;
  uint32_t ack;
  uint32_t mask;
  uint32_t mask_set;
  uint32_t mask_clr;
  uint32_t sens;
  uint32_t sens_set;
  uint32_t sens_clr;
  uint32_t soft;
  uint32_t soft_set;
  uint32_t soft_clr;
  uint32_t pol;
  uint32_t pol_set;
  uint32_t pol_clr;
  uint32_t dom_en;
  uint32_t dbnc_ctrl;
  uint32_t dbnc_set;
  uint32_t dbnc_clr;
  uint32_t raw_stat;
};

struct mtk_eint_pin_desc {
  uint16_t pin;
  uint8_t instance;
  uint8_t index;
  uint8_t debounce;
};

struct mtk_eint_hw_desc {
  const char *name;
  uint32_t total_pins;
  uint32_t instance_count;
  const char *const *instance_names;
  const struct mtk_eint_regs *regs;
  const struct mtk_eint_pin_desc *pins;
  size_t pin_count;
};

extern const struct mtk_eint_hw_desc mt8668_eint_hw;

__END_CDECLS;
