// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <fbl/unique_fd.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/zx/channel.h>
#include <lib/zx/resource.h>
#include <zircon/device/trusty-vdev.h>

#include <gzos/ipc/cpp/fidl.h>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/logging.h"
#include "garnet/bin/gzos/ree_agent/gz_ipc_client.h"

constexpr const char kTrustyVirtioPath[] = "/dev/class/tee/000";

class ServiceProviderImpl : public gzos::ipc::ServiceProvider {
 public:
  ServiceProviderImpl(zx_handle_t shm_handle, zx::channel message_channel,
                      size_t max_message_size)
      : context_(component::StartupContext::CreateFromStartupInfo()),
        client_(zx::unowned_resource(shm_handle), std::move(message_channel),
                max_message_size),
        shm_rsc_(shm_handle) {
    context_->outgoing().AddPublicService(bindings_.GetHandler(this));

    FXL_CHECK(client_.Start() == ZX_OK);
  }

 private:
  void ConnectToService(fidl::StringPtr service_name,
                        zx::channel channel) override {
    client_.Connect(service_name.get(), std::move(channel));
  }

  std::unique_ptr<component::StartupContext> context_;
  ree_agent::GzIpcClient client_;
  zx::resource shm_rsc_;

  fidl::BindingSet<gzos::ipc::ServiceProvider> bindings_;
};

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  fbl::unique_fd fd(open(kTrustyVirtioPath, O_RDWR));
  if (!fd) {
    FXL_LOG(ERROR) << "Failed to open " << kTrustyVirtioPath;
    return ZX_ERR_IO;
  }

  zx_handle_t client_handle;
  ssize_t n = ioctl_trusty_vdev_start(fd.get(), &client_handle);
  if (n < 0) {
    FXL_LOG(ERROR) << "Failed to start trusty vdev, n=" << n;
    return ZX_ERR_IO;
  }

  size_t max_message_size;
  n = ioctl_trusty_vdev_get_message_size(fd.get(), &max_message_size);
  if (n < 0) {
    FXL_LOG(ERROR) << "Failed to get max_message_size, n=" << n;
    return ZX_ERR_IO;
  }

  zx_handle_t shm_handle;
  n = ioctl_trusty_vdev_get_shm_resource(fd.get(), &shm_handle);
  if (n < 0) {
    FXL_LOG(ERROR) << "Failed to get shm resource, n=" << n;
    return ZX_ERR_IO;
  }

  zx::channel message_channel(client_handle);
  ServiceProviderImpl service(shm_handle, std::move(message_channel),
                              max_message_size);

  loop.Run();
  return 0;
}
