#include "display_manager_impl.h"
#include "lib/fxl/logging.h"

namespace display {
DisplayManagerImpl::DisplayManagerImpl()
    : DisplayManagerImpl(
          fuchsia::sys::StartupContext::CreateFromStartupInfo()) {}

DisplayManagerImpl::DisplayManagerImpl(
    std::unique_ptr<fuchsia::sys::StartupContext> context)
    : context_(std::move(context)) {
  display_ = std::unique_ptr<Display>(Display::GetDisplay());

  context_->outgoing().AddPublicService<DisplayManager>(
      [this](fidl::InterfaceRequest<DisplayManager> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

void DisplayManagerImpl::GetBrightness(GetBrightnessCallback callback) {
  if (NULL == display_.get()) {
    FXL_LOG(ERROR) << "GetBrightness: display not retrieved";
    callback(false, 0.0f);
    return;
  }

  double brightness;
  callback(display_->GetBrightness(&brightness), brightness);
}

void DisplayManagerImpl::SetBrightness(double brightness,
                                       SetBrightnessCallback callback) {
  if (NULL == display_.get()) {
    FXL_LOG(ERROR) << "SetBrightness: display not retrieved";
    callback(false);
    return;
  }

  callback(display_->SetBrightness(brightness));
}
}  // namespace display