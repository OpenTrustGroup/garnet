// Copyright 2020 GoldenRiver Technology Co., Ltd. All rights reserved.

/**
 * Utils function for NSC.
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <zircon/threads.h>

#include <trusty_ipc.h>

extern uint32_t OPEN_FLAGS[18];

extern uint32_t OPEN_MODES[9];

extern uint32_t SEEK_FLAGS[3];

/**
 * Commands between NSC client and server in HEE.
 * Refer to "nsc_interface.h"
 */
enum {
  NBL_NSC_CMD_START_CLIENT = 1,
  NBL_NSC_CMD_FINISH_START_CLIENT = 2,
};


// Extract from "_nss_get_client_name" in "nsc_srv.c".
int nbl_nsc_get_client_name();

// Extract from "_send_finish_start_port" in "nsc_srv.c".
int nbl_nsc_send_finish_start_port();
