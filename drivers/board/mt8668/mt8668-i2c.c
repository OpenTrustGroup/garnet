// SPDX-License-Identifier: BSD-3-Clause

#include <ddk/protocol/platform-defs.h>
#include <zircon/types.h>

#include "mt8668-dts-pbus.h"

enum {
    MT8668_I2C_DID = 17,
};

zx_status_t mt8668_board_i2c_register_from_dts(const mt8668_dts_pbus_t* dts) {
    static const mt8668_dts_pbus_device_t i2c = {
        .compatible = "mediatek,mt6991-i2c",
        .required_mmios = 2,
        .required_irqs = 1,
        .vid = PDEV_VID_MTK,
        .pid = PDEV_PID_MT8668,
        .did = MT8668_I2C_DID,
        .bti = MT8668_DTS_BTI_INDEXED,
    };

    return mt8668_dts_pbus_add_alias_prefix(dts, &i2c, "i2c");
}
