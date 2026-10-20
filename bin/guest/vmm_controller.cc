// SPDX-License-Identifier: BSD-3-Clause

#include "garnet/bin/guest/vmm_controller.h"

#include <zircon/processargs.h>

#include <memory>
#include <utility>

namespace {

const char* VcpuExitToString(zx_status_t status) {
  switch (status) {
    case ZX_ERR_CANCELED:
      return "reboot_required";
    case ZX_ERR_UNAVAILABLE:
      return "shutdown";
    default:
      return "internal_error";
  }
}

const char* GuestErrorToString(GuestError status) {
  switch (status) {
    case GuestError::OK:
      return "ok";
    case GuestError::NOT_FOUND:
      return "not_found";
    case GuestError::ALREADY_RUNNING:
      return "already_running";
    case GuestError::REBOOT_REQUIRED:
      return "reboot_required";
    case GuestError::INITIALIZATION_FAILED:
      return "initialization_failed";
    case GuestError::START_VCPU_FAILED:
      return "start_vcpu_failed";
    case GuestError::OUT_OF_MEMORY:
      return "out_of_memory";
    case GuestError::FORCE_STOPPED:
      return "force_stopped";
    case GuestError::SHUTDOWN:
      return "shutdown";
    case GuestError::INTERNAL_ERROR:
      return "internal_error";
  }
  return "unknown";
}

}  // namespace

VmmController::VmmController(component::ApplicationContext* app_context,
                             async_t* async,
                             std::function<void()> stop_callback)
    : application_context_(app_context),
      async_(async),
      stop_component_callback_(std::move(stop_callback)) {
  FXL_CHECK(stop_component_callback_);
  bindings_.set_empty_set_handler([this]() { LifecycleChannelClosed(); });

  auto server_channel = zx::channel(zx_get_startup_handle(PA_HND(PA_USER0, 0)));
  fidl::InterfaceRequest<GuestLifecycle> request(std::move(server_channel));
  bindings_.AddBinding(this, std::move(request));
}

VmmController::~VmmController() {
  ClearPendingCreate();
}

void VmmController::Create(virtualization::Config config,
                           CreateCallback callback) {
  FXL_LOG(INFO) << "event=vmm_create_request vmid=" << config.vmid
                << " has_vmm=" << static_cast<bool>(vmm_)
                << " has_run_callback=" << static_cast<bool>(run_callback_);
  if (vmm_ || run_callback_ || create_pending_ || teardown_.create_blocked()) {
    callback(GuestError::ALREADY_RUNNING);
    return;
  }

  auto vmm = std::make_unique<Vmm>(application_context_);
  if (!vmm) {
    callback(GuestError::OUT_OF_MEMORY);
    return;
  }

  create_pending_ = true;
  pending_vmm_ = std::move(vmm);
  pending_config_ =
      std::make_unique<virtualization::Config>(std::move(config));
  pending_create_callback_ = std::move(callback);
  pending_feature_request_ = LoadGuestFeatures(
      application_context_,
      [this](zx_status_t status, GuestFeatures features) {
        FinishCreate(status, features);
      });
}

void VmmController::FinishCreate(zx_status_t status,
                                 GuestFeatures features) {
  if (!create_pending_) {
    return;
  }

  create_pending_ = false;
  pending_feature_request_ = nullptr;
  auto vmm = std::move(pending_vmm_);
  auto config = std::move(pending_config_);
  CreateCallback callback = std::move(pending_create_callback_);

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "event=guest_features_load_failed zx_status=" << status;
    callback(GuestError::INITIALIZATION_FAILED);
    return;
  }

  status = vmm->Initialize(std::move(*config), features);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "event=vmm_initialize_failed zx_status=" << status;
    vmm->Shutdown([callback = std::move(callback)]() mutable {
      callback(GuestError::INITIALIZATION_FAILED);
    });
    return;
  }

  FXL_LOG(INFO) << "event=vmm_create_succeeded";
  vmm_ = std::move(vmm);
  callback(GuestError::OK);
}

void VmmController::CancelPendingCreate(GuestError status) {
  if (!create_pending_) {
    return;
  }

  create_pending_ = false;
  pending_vmm_.reset();
  pending_config_.reset();
  if (pending_feature_request_) {
    pending_feature_request_->Cancel();
    pending_feature_request_ = nullptr;
  }
  CreateCallback callback = std::move(pending_create_callback_);
  callback(status);
}

void VmmController::ClearPendingCreate() {
  create_pending_ = false;
  pending_vmm_.reset();
  pending_config_.reset();
  pending_create_callback_ = CreateCallback();
  if (pending_feature_request_) {
    pending_feature_request_->Cancel();
    pending_feature_request_ = nullptr;
  }
}

void VmmController::Run(RunCallback callback) {
  FXL_LOG(INFO) << "event=vmm_run_request has_vmm=" << static_cast<bool>(vmm_)
                << " run_callback_bound=" << static_cast<bool>(run_callback_);
  if (!vmm_) {
    callback(GuestError::NOT_FOUND);
    return;
  }

  if (run_callback_) {
    callback(GuestError::ALREADY_RUNNING);
    return;
  }

  auto status = vmm_->StartPrimaryVcpu([this](zx_status_t result) {
    GuestError error;

    if (result == ZX_ERR_CANCELED) {
      error = GuestError::REBOOT_REQUIRED;
    } else if (result == ZX_ERR_UNAVAILABLE) {
      error = GuestError::SHUTDOWN;
    } else {
      error = GuestError::INTERNAL_ERROR;
    }
    FXL_LOG(INFO) << "event=primary_vcpu_exit zx_status=" << result
                  << " stop_reason=" << VcpuExitToString(result)
                  << " mapped_status=" << GuestErrorToString(error);
    ScheduleVmmTeardown(error);
  });
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "event=primary_vcpu_start_failed zx_status=" << status;
    vmm_->Shutdown([this, callback = std::move(callback)]() mutable {
      vmm_.reset();
      callback(GuestError::START_VCPU_FAILED);
    });
    return;
  }

  FXL_LOG(INFO) << "event=primary_vcpu_started";
  run_callback_ = std::move(callback);
}

void VmmController::Bind(fidl::InterfaceRequest<GuestController> request,
                         BindCallback callback) {
  if (vmm_) {
    vmm_->AddBinding(std::move(request));
    callback(GuestError::OK);
  } else {
    callback(GuestError::NOT_FOUND);
  }
}

void VmmController::Stop(StopCallback callback) {
  FXL_LOG(INFO) << "event=vmm_stop_request source=client_request has_vmm="
                << static_cast<bool>(vmm_);
  CancelPendingCreate(GuestError::FORCE_STOPPED);
  ScheduleVmmTeardown(GuestError::FORCE_STOPPED);
  callback();
}

void VmmController::LifecycleChannelClosed() {
  teardown_.RecordLifecycleClose();

  if (create_pending_ || vmm_ || run_callback_ || teardown_.teardown_active()) {
    CancelPendingCreate(GuestError::FORCE_STOPPED);
    ScheduleVmmTeardown(GuestError::FORCE_STOPPED);
    return;
  }

  MaybeStopComponent();
}

void VmmController::ScheduleVmmTeardown(GuestError status) {
  if (!teardown_.RequestTeardown()) {
    return;
  }

  auto result =
      async::PostTask(async_, [this, status]() { DestroyAndRespond(status); });

  // If ZX_OK, the task was successfully scheduled. If ZX_ERR_BAD_STATE, the
  // component is already shutting down. Run teardown inline so pending run
  // callbacks and lifecycle-stop completion are not lost.
  if (result != ZX_OK) {
    FXL_LOG(WARNING) << "event=vmm_teardown_schedule_failed async_status="
                     << result;
    DestroyAndRespond(status);
  }
}

void VmmController::DestroyAndRespond(GuestError status) {
  CancelPendingCreate(status);
  if (!teardown_.BeginTeardown()) {
    return;
  }

  if (vmm_) {
    vmm_->NotifyClientsShutdown();
    vmm_->Shutdown([this, status]() { FinishVmmTeardown(status); });
    return;
  }

  FinishVmmTeardown(status);
}

void VmmController::FinishVmmTeardown(GuestError status) {
  if (!teardown_.FinishTeardown()) {
    return;
  }

  vmm_.reset();

  if (run_callback_) {
    RunCallback callback = std::move(run_callback_);
    callback(status);
  }

  MaybeStopComponent();
}

void VmmController::MaybeStopComponent() {
  if (!teardown_.CompleteComponentStop()) {
    return;
  }

  stop_component_callback_();
}
