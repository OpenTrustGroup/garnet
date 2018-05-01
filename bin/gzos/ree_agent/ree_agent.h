#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <lib/async/cpp/wait.h>

#include <fs/managed-vfs.h>
#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/vfs.h>
#include <lib/async-loop/cpp/loop.h>

#include "lib/fxl/logging.h"
#include "lib/svc/cpp/service_provider_bridge.h"

namespace ree_agent {

class ReeAgent {
 public:
  auto services() { return &services_; }

 private:
  component::ServiceProviderBridge services_;
};

}  // namespace ree_agent
