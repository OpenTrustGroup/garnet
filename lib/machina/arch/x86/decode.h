// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_ARCH_X86_DECODE_H_
#define GARNET_LIB_MACHINA_ARCH_X86_DECODE_H_

#include <zircon/types.h>

// clang-format off

#define X86_FLAGS_STATUS    ((1u << 11 /* OF */) |                  \
                             (1u << 7 /* SF */) |                   \
                             (1u << 6 /* ZF */) |                   \
                             (1u << 2 /* PF */) |                   \
                             (1u << 1 /* Reserved (must be 1) */) | \
                             (1u << 0 /* CF */))

#define INST_MOV_READ       0u
#define INST_MOV_WRITE      1u
#define INST_TEST           2u

// clang-format on

typedef struct zx_vcpu_state zx_vcpu_state_t;

namespace machina {

// Stores info from a decoded instruction.
struct Instruction {
  uint8_t type;
  uint8_t access_size;
  uint32_t imm;
  uint64_t* reg;
  uint64_t* flags;
};

zx_status_t inst_decode(const uint8_t* inst_buf, uint32_t inst_len,
                        uint8_t default_operand_size,
                        zx_vcpu_state_t* vcpu_state, Instruction* inst);

#define DEFINE_INST_VAL(size)                                            \
  static inline uint##size##_t inst_val##size(const Instruction* inst) { \
    return (uint##size##_t)(inst->reg != NULL ? *inst->reg : inst->imm); \
  }
DEFINE_INST_VAL(32);
DEFINE_INST_VAL(16);
DEFINE_INST_VAL(8);
#undef DEFINE_INST_VAL

#define DEFINE_INST_READ(size)                                          \
  static inline zx_status_t inst_read##size(const Instruction* inst,    \
                                            uint##size##_t value) {     \
    if (inst->type != INST_MOV_READ || inst->access_size != (size / 8)) \
      return ZX_ERR_NOT_SUPPORTED;                                      \
    *inst->reg = value;                                                 \
    return ZX_OK;                                                       \
  }
DEFINE_INST_READ(32);
DEFINE_INST_READ(16);
DEFINE_INST_READ(8);
#undef DEFINE_INST_READ

#define DEFINE_INST_WRITE(size)                                          \
  static inline zx_status_t inst_write##size(const Instruction* inst,    \
                                             uint##size##_t* value) {    \
    if (inst->type != INST_MOV_WRITE || inst->access_size != (size / 8)) \
      return ZX_ERR_NOT_SUPPORTED;                                       \
    *value = inst_val##size(inst);                                       \
    return ZX_OK;                                                        \
  }
DEFINE_INST_WRITE(32);
DEFINE_INST_WRITE(16);
DEFINE_INST_WRITE(8);
#undef DEFINE_INST_WRITE

#if defined(__x86_64__)
// Returns the flags that are assigned to the x86 flags register by an
// 8-bit TEST instruction for the given two operand values.
static inline uint16_t x86_flags_for_test8(uint8_t value1, uint8_t value2) {
  // TEST cannot set the overflow flag (bit 11).
  uint16_t ax_reg;
  __asm__ volatile(
      "testb %[i1], %[i2];"
      "lahf"  // Copies flags into the %ah register
      : "=a"(ax_reg)
      : [i1] "r"(value1), [i2] "r"(value2)
      : "cc");
  // Extract the value of the %ah register from the %ax register.
  return (uint16_t)(ax_reg >> 8);
}
#endif

static inline zx_status_t inst_test8(const Instruction* inst, uint8_t inst_val,
                                     uint8_t value) {
  if (inst->type != INST_TEST || inst->access_size != 1u ||
      inst_val8(inst) != inst_val) {
    return ZX_ERR_NOT_SUPPORTED;
  }
#if __x86_64__
  *inst->flags &= ~X86_FLAGS_STATUS;
  *inst->flags |= x86_flags_for_test8(inst_val, value);
  return ZX_OK;
#else   // __x86_64__
  return ZX_ERR_NOT_SUPPORTED;
#endif  // __x86_64__
}

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_ARCH_X86_DECODE_H_
