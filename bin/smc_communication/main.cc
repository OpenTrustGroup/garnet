// SPDX-License-Identifier: BSD-3-Clause

#include "lib/fxl/logging.h"
#include "smc_communication.h"

int main(int argc, char** argv) {
  FXL_LOG(INFO) << "Starting smc communication service...";

  SmcCommunication* smc_communication = SmcCommunication::GetInstance();
  zx_status_t status = smc_communication->Initialize();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to initialize smc communication: " << status;
    return 1;
  }

  return 0;
}
