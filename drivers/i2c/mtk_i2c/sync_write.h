// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#define DSB __asm__ volatile("dsb sy" ::: "memory")
#define ISB __asm__ volatile("isb" ::: "memory")

#define dsb()  DSB

#define mt_reg_sync_writel(v, a) \
        do {    \
            *(volatile unsigned int *)(a) = (v);    \
            dsb(); \
        } while (0)

#define mt_reg_sync_writew(v, a) \
        do {    \
            *(volatile unsigned short *)(a) = (v);    \
            dsb(); \
        } while (0)

#define mt_reg_sync_writeb(v, a) \
        do {    \
            *(volatile unsigned char *)(a) = (v);    \
            dsb(); \
        } while (0)

/*==== EXPORTED MACRO ===================================================*/

#define MAXIMUM(A,B)                (((A)>(B))?(A):(B))
#define MINIMUM(A,B)                (((A)<(B))?(A):(B))

#define READ_REGISTER_UINT32(reg) \
    (*(volatile uint32_t * const)(reg))

#define WRITE_REGISTER_UINT32(reg, val) \
    (*(volatile uint32_t * const)(reg)) = (val)

#define READ_REGISTER_UINT16(reg) \
    (*(volatile uint16_t * const)(reg))

#define WRITE_REGISTER_UINT16(reg, val) \
    (*(volatile uint16_t * const)(reg)) = (val)

#define READ_REGISTER_UINT8(reg) \
    (*(volatile uint8_t * const)(reg))

#define WRITE_REGISTER_UINT8(reg, val) \
    (*(volatile uint8_t * const)(reg)) = (val)

#define INREG8(x)                   READ_REGISTER_UINT8((uint8_t*)(x))
#define OUTREG8(x, y)               WRITE_REGISTER_UINT8((uint8_t*)(x), (uint8_t)(y))
#define SETREG8(x, y)               OUTREG8(x, INREG8(x)|(y))
#define CLRREG8(x, y)               OUTREG8(x, INREG8(x)&~(y))
#define MASKREG8(x, y, z)           OUTREG8(x, (INREG8(x)&~(y))|(z))

#define INREG16(x)                  READ_REGISTER_UINT16((uint16_t*)(x))
#define OUTREG16(x, y)              WRITE_REGISTER_UINT16((uint16_t*)(x),(uint16_t)(y))
#define SETREG16(x, y)              OUTREG16(x, INREG16(x)|(y))
#define CLRREG16(x, y)              OUTREG16(x, INREG16(x)&~(y))
#define MASKREG16(x, y, z)          OUTREG16(x, (INREG16(x)&~(y))|(z))

#define INREG32(x)                  READ_REGISTER_UINT32((uint32_t*)(x))
#define OUTREG32(x, y)              WRITE_REGISTER_UINT32((uint32_t*)(x), (uint32_t)(y))
#define SETREG32(x, y)              OUTREG32(x, INREG32(x)|(y))
#define CLRREG32(x, y)              OUTREG32(x, INREG32(x)&~(y))
#define MASKREG32(x, y, z)          OUTREG32(x, (INREG32(x)&~(y))|(z))


#define DRV_Reg8(addr)              INREG8(addr)
#define DRV_WriteReg8(addr, data)   OUTREG8(addr, data)
#define DRV_SetReg8(addr, data)     SETREG8(addr, data)
#define DRV_ClrReg8(addr, data)     CLRREG8(addr, data)

#define DRV_Reg16(addr)             INREG16(addr)
#define DRV_WriteReg16(addr, data)  OUTREG16(addr, data)
#define DRV_SetReg16(addr, data)    SETREG16(addr, data)
#define DRV_ClrReg16(addr, data)    CLRREG16(addr, data)

#define DRV_Reg32(addr)             INREG32(addr)
#define DRV_WriteReg32(addr, data)  OUTREG32(addr, data)
#define DRV_SetReg32(addr, data)    SETREG32(addr, data)
#define DRV_ClrReg32(addr, data)    CLRREG32(addr, data)

// !!! DEPRECATED, WILL BE REMOVED LATER !!!
#define DRV_Reg(addr)               DRV_Reg16(addr)
#define DRV_WriteReg(addr, data)    DRV_WriteReg16(addr, data)
#define DRV_SetReg(addr, data)      DRV_SetReg16(addr, data)
#define DRV_ClrReg(addr, data)      DRV_ClrReg16(addr, data)

#define __raw_readb(REG)            DRV_Reg8(REG)
#define __raw_readw(REG)            DRV_Reg16(REG)
#define __raw_readl(REG)            DRV_Reg32(REG)
#define __raw_writeb(VAL, REG)      DRV_WriteReg8(REG,VAL)
#define __raw_writew(VAL, REG)      DRV_WriteReg16(REG,VAL)
#define __raw_writel(VAL, REG)      DRV_WriteReg32(REG,VAL)