#ifndef GARNET_BIN_A11Y_A11Y_MANAGER_APP_H_
#define GARNET_BIN_A11Y_A11Y_MANAGER_APP_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>

#include "garnet/bin/a11y/a11y_manager/manager_impl.h"
#include "lib/app/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/logging.h"

namespace a11y_manager {

// A11y manager application entry point.
class App {
 public:
  explicit App();
  ~App();

 private:
  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;

  std::unique_ptr<ManagerImpl> a11y_manager_;
  fidl::BindingSet<fuchsia::accessibility::Manager> binding_set_;

  FXL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace a11y_manager

#endif  // GARNET_BIN_A11Y_A11Y_MANAGER_APP_H_
