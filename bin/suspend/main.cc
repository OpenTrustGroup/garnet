#include "lib/fxl/logging.h"
#include "device_suspend.h"

int main(int argc, char** argv) {
  FXL_LOG(INFO) << "Starting device suspend service...";
  DeviceSuspend* device_suspend = DeviceSuspend::GetInstance();
  zx_status_t status = device_suspend->Initialize();

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to initialize DeviceSuspend: " << status;
    return 1;
  }

  return 0;
}
