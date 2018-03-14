// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_ARCH_X86_ACPI_PRIV_H_
#define GARNET_LIB_MACHINA_ARCH_X86_ACPI_PRIV_H_

#include <acpica/acpi.h>
#include <acpica/actypes.h>

/* PM register addresses. */
#define PM1A_REGISTER_STATUS 0
#define PM1A_REGISTER_ENABLE (ACPI_PM1_REGISTER_WIDTH / 8)

#endif  // GARNET_LIB_MACHINA_ARCH_X86_ACPI_PRIV_H_
