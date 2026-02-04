
// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "garnet/bin/appmgr/job_holder.h"
#include <fcntl.h>
#include <fdio/namespace.h>
#include <fdio/util.h>
#include <launchpad/launchpad.h>
#include <lib/async/default.h>
#include <lib/zx/process.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <utility>

#include "garnet/bin/appmgr/dynamic_library_loader.h"
#include "garnet/bin/appmgr/namespace_builder.h"
#include "garnet/bin/appmgr/runtime_metadata.h"
#include "garnet/bin/appmgr/url_resolver.h"
#include "garnet/lib/far/format.h"
#include "lib/app/cpp/connect.h"
#include "lib/fsl/handles/object_info.h"
#include "lib/fsl/io/fd.h"
#include "lib/fsl/vmo/file.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/svc/cpp/services.h"

#ifdef __Nebula__
#include "nbl/grt_trusty_helper.h"
#include "nbl/otrp_vfs_proxy.h"
#include <zircon/syscalls.h>
#include <zircon/syscalls/policy.h>
#endif

namespace component {
namespace {

constexpr zx_rights_t kChildJobRights = ZX_RIGHTS_BASIC | ZX_RIGHTS_IO;

constexpr char kNumberedLabelFormat[] = "env-%d";
constexpr char kAppPath[] = "bin/app";
constexpr char kAppArv0[] = "/pkg/bin/app";
constexpr char kLegacyFlatExportedDirPath[] = "meta/legacy_flat_exported_dir";
constexpr char kRuntimePath[] = "meta/runtime";
constexpr char kSandboxPath[] = "meta/sandbox";

enum class LaunchType {
  kProcess,
  kArchive,
};

std::vector<const char*> GetArgv(const std::string& argv0,
                                 const ApplicationLaunchInfo& launch_info) {
  std::vector<const char*> argv;
  argv.reserve(launch_info.arguments->size() + 1);
  argv.push_back(argv0.c_str());
  for (const auto& argument : *launch_info.arguments)
    argv.push_back(argument.get().c_str());
  return argv;
}

// The very first nested environment process we create gets the
// PA_DIRECTORY_REQUEST given to us by our parent. It's slightly awkward that we
// don't publish the root environment's services. We should consider
// reorganizing the boot process so that the root environment's services are
// the ones we want to publish.
void PublishServicesForFirstNestedEnvironment(ServiceProviderBridge* services) {
  static zx_handle_t request = zx_get_startup_handle(PA_DIRECTORY_REQUEST);
  if (request == ZX_HANDLE_INVALID)
    return;
  services->ServeDirectory(zx::channel(request));
  request = ZX_HANDLE_INVALID;
}

std::string GetLabelFromURL(const std::string& url) {
  size_t last_slash = url.rfind('/');
  if (last_slash == std::string::npos || last_slash + 1 == url.length())
    return url;
  return url.substr(last_slash + 1);
}

void PushFileDescriptor(component::FileDescriptorPtr fd,
                        int new_fd,
                        std::vector<uint32_t>* ids,
                        std::vector<zx_handle_t>* handles) {
  if (!fd)
    return;
  if (fd->type0) {
    ids->push_back(PA_HND(PA_HND_TYPE(fd->type0), new_fd));
    handles->push_back(fd->handle0.release());
  }
  if (fd->type1) {
    ids->push_back(PA_HND(PA_HND_TYPE(fd->type1), new_fd));
    handles->push_back(fd->handle1.release());
  }
  if (fd->type2) {
    ids->push_back(PA_HND(PA_HND_TYPE(fd->type2), new_fd));
    handles->push_back(fd->handle2.release());
  }
}

#ifdef __Nebula__

static OtrpVfsProxy otrp_vfs_proxy;
static zx_handle_t otrp_root_channel = ZX_HANDLE_INVALID;
static bool otrp_vfs_proxy_init = false;

bool need_svc(const char* url) {

  if (is_trusty_app(url)) {
    // trusty app can see "svc" folder, but use "white list services".
    return true;
  }

  if (is_loader(url) || is_gnapp_loader(url) ||
      strcmp(url, "file://sysmgr") == 0 ||
      strcmp(url, "file://uos") == 0 ||
      strcmp(url, "file://sos") == 0 ||
      strcmp(url, "file://netstack") == 0 ||
      strcmp(url, "file://trace_manager") == 0 ||
      strcmp(url, "file://ktrace_provider") == 0 ||
      strcmp(url, "file://nbl_activity_lifecycle_srv") == 0)
    return true;

  return false;
}

static bool is_secmgr(const char* url) {
  return strcmp(url, "heeta://system/bin/nbl_secmgr_srv") == 0;
}
#endif

#ifdef __Nebula__
zx::process CreateProcess(JobHolder *holder,
                          fsl::SizedVmo data,
                          const std::string& argv0,
                          ApplicationLaunchInfo launch_info,
                          zx::channel loader_service,
                          fdio_flat_namespace_t* flat,
                          ApplicationPackage* package,
                          int *rc) {
#else
zx::process CreateProcess(const zx::job& job,
                          fsl::SizedVmo data,
                          const std::string& argv0,
                          ApplicationLaunchInfo launch_info,
                          zx::channel loader_service,
                          fdio_flat_namespace_t* flat) {
#endif
  if (!data)
    return zx::process();

  std::string label = GetLabelFromURL(launch_info.url);
  std::vector<const char*> argv = GetArgv(argv0, launch_info);

  std::vector<uint32_t> ids;
  std::vector<zx_handle_t> handles;

  zx::channel directory_request = std::move(launch_info.directory_request);
  if (directory_request) {
    ids.push_back(PA_DIRECTORY_REQUEST);
    handles.push_back(directory_request.release());
  }

  if (launch_info.ta_session_request.is_valid()) {
    ids.push_back(PA_HND(PA_USER0, 0));
    handles.push_back(launch_info.ta_session_request.release());
  }

  PushFileDescriptor(std::move(launch_info.out), STDOUT_FILENO, &ids,
                     &handles);
  PushFileDescriptor(std::move(launch_info.err), STDERR_FILENO, &ids,
                     &handles);

  for (size_t i = 0; i < flat->count; ++i) {
    ids.push_back(flat->type[i]);
    handles.push_back(flat->handle[i]);
  }

  data.vmo().set_property(ZX_PROP_NAME, label.data(), label.size());

#ifdef __Nebula__
  uint32_t heap_size = NBL_APP_DEFAULT_HEAP_SIZE;
  uuid_t uuid;
  bool flg_restart  = package->trusty_flags & NEBULA_APP_FLAGS_RESTART_ON_EXIT;
  bool flg_create_process = package->trusty_flags & NEBULA_APP_FLAGS_NEW_PROCESS;
  bool flg_call_trusty_api = package->trusty_flags & NEBULA_APP_FLAGS_CALL_TRUSTY_API;
  zx::job *child_job = nullptr;
  uint32_t *mmios = nullptr;

  if (is_trusty_app(launch_info.url->c_str())) {
    memset(&uuid, 0, sizeof(uuid_t));
    get_trusty_app_uuid(package, &uuid);
  }
#endif

  // TODO(abarth): We probably shouldn't pass environ, but currently this
  // is very useful as a way to tell the loader in the child process to
  // print out load addresses so we can understand crashes.
  launchpad_t* lp = nullptr;
#ifdef __Nebula__
  if (is_trusty_app(launch_info.url->c_str())) {
    if (flg_restart || is_secmgr(launch_info.url->c_str())) {
      child_job = holder->create_job_service(flg_create_process, flg_call_trusty_api);
    } else {
      child_job = holder->create_job_one_shot(flg_create_process, flg_call_trusty_api);
    }
    launchpad_create(child_job->get(), label.c_str(), &lp);
  } else {
    launchpad_create(holder->job().get(), label.c_str(), &lp);
  }

  if (is_secmgr(launch_info.url->c_str())) {
    zx_status_t status = ZX_OK;
    zx_handle_t secmgr_chans[2];
    if (otrp_root_channel == ZX_HANDLE_INVALID) {
      otrp_root_channel = zx_get_startup_handle(PA_HND(PA_USER0, 0));
    }
    zx_channel_create(0, &secmgr_chans[0], &secmgr_chans[1]);
    if (!otrp_vfs_proxy_init) {
      status = otrp_vfs_proxy.Init(otrp_root_channel, secmgr_chans[0]);
      if (status == ZX_OK) {
        otrp_vfs_proxy_init = true;
      }
    } else {
      otrp_vfs_proxy.ResetSecmgrRequestChannel(secmgr_chans[0]);
    }
    if (status == ZX_OK) {
      ids.push_back(PA_HND(PA_USER1, 0));
      handles.push_back(secmgr_chans[1]);
    }
  }
#else
  launchpad_create(job.get(), label.c_str(), &lp);
#endif


  launchpad_clone(lp, LP_CLONE_ENVIRON);
  launchpad_clone_fd(lp, STDIN_FILENO, STDIN_FILENO);
  if (!launch_info.out)
    launchpad_clone_fd(lp, STDOUT_FILENO, STDOUT_FILENO);
  if (!launch_info.err)
    launchpad_clone_fd(lp, STDERR_FILENO, STDERR_FILENO);
  if (loader_service)
    launchpad_use_loader_service(lp, loader_service.release());
  launchpad_set_args(lp, argv.size(), argv.data());
  launchpad_set_nametable(lp, flat->count, flat->path);
  launchpad_add_handles(lp, handles.size(), handles.data(), ids.data());
  launchpad_load_from_vmo(lp, data.vmo().release());

#ifdef __Nebula__
  if (is_trusty_app(launch_info.url->c_str())) {
    auto cleanup = fxl::MakeAutoCall([&]() {
      launchpad_destroy(lp);
      delete child_job;
      delete[] mmios;
    });

    uint32_t mmio_size = 0;
    if (package->mmio_infos) {
      const std::vector<uint32_t>& mmio_infos = package->mmio_infos;
      mmio_size = mmio_infos.size();
      if (mmio_size) {
        mmios = new uint32_t[mmio_size];
        for (uint32_t i = 0; (i + 2) < mmio_size; i += 3) {
          mmios[i] = mmio_infos[i];
          mmios[i + 1] = mmio_infos[i + 1];
          mmios[i + 2] = mmio_infos[i + 2];
          if (!trusty_mem_map_check(&uuid, mmio_infos[i + 1],
                                    mmio_infos[i + 2])) {
            *rc = ZX_ERR_INVALID_ARGS;
            return zx::process();
          }
        }
      }
    }

    zx_handle_t heap_vmo = ZX_HANDLE_INVALID;
    if (!launch_info.ion_heap_args.is_null()) {
      heap_vmo =
          create_ion_heap_vmo(launch_info.url, launch_info.ion_heap_args.get());
    } else {
      if (package->heap_size > 0) {
        heap_size = package->heap_size;
      }
      heap_vmo = create_heap_vmo(heap_size);
    }
    if (heap_vmo == ZX_HANDLE_INVALID) {
      FXL_LOG(ERROR) << "Cannot create application's heap vmo.";
      *rc = ZX_ERR_NO_RESOURCES;
      return zx::process();
    }
    launchpad_add_handle(lp, heap_vmo, PA_HND(PA_USER2, 0));

    zx_handle_t process = launchpad_get_process_handle(lp);
    zx_status_t trusty_status = zx_trusty_set_config(
        process, &uuid, sizeof(uuid_t), mmios, mmio_size, package->irq);
    if (trusty_status != ZX_OK) {
      FXL_LOG(ERROR) << "Cannot set application's trusty config.";
      *rc = ZX_ERR_INVALID_ARGS;
      return zx::process();
    }

    cleanup.cancel();
    enable_trusty_irq(process);

    // Job object should be deleted, because the job is duplicated and
    // transferred into child process. If not, kill job or sub process of job,
    // the job will not be deleted, because ref count > 0.
    delete child_job;
    delete[] mmios;
  }
#endif

  zx_handle_t proc;
  const char* errmsg;
  zx_handle_t status = launchpad_go(lp, &proc, &errmsg);
  if (status != ZX_OK) {
#ifdef __Nebula__
    *rc = ZX_ERR_WRONG_TYPE;
#endif
    FXL_LOG(ERROR) << "Cannot run executable " << label << " due to error "
                   << status << " (" << zx_status_get_string(status)
                   << "): " << errmsg;
    return zx::process();
  }
  return zx::process(proc);
}

LaunchType Classify(const zx::vmo& data, std::string* runner) {
  if (!data)
    return LaunchType::kProcess;
  std::string magic(archive::kMagicLength, '\0');
  zx_status_t status = data.read(&magic[0], 0, magic.length());
  if (status != ZX_OK)
    return LaunchType::kProcess;
  if (memcmp(magic.data(), &archive::kMagic, sizeof(archive::kMagic)) == 0)
    return LaunchType::kArchive;
  return LaunchType::kProcess;
}

struct ExportedDirChannels {
  // The client side of the channel serving connected application's exported
  // dir.
  zx::channel exported_dir;

  // The server side of our client's |ApplicationLaunchInfo.directory_request|.
  zx::channel client_request;
};

ExportedDirChannels BindDirectory(ApplicationLaunchInfo* launch_info) {
  zx::channel exported_dir_server, exported_dir_client;
  zx_status_t status =
      zx::channel::create(0u, &exported_dir_server, &exported_dir_client);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create channel for service directory: status="
                   << status;
    return {zx::channel(), zx::channel()};
  }

  auto client_request = std::move(launch_info->directory_request);
  launch_info->directory_request = std::move(exported_dir_server);
  return {std::move(exported_dir_client), std::move(client_request)};
}

}  // namespace

uint32_t JobHolder::next_numbered_label_ = 1u;

zx::job* JobHolder::create_job_one_shot(bool flg_create_process,
                                        bool flg_call_trusty_api) {
  zx::job sub_job;  // freed in end of function.
  zx::job* limited_sub_job = new zx::job();
  FXL_CHECK(zx::job::create(job_child_one_shot_.get(), 0u, &sub_job) == ZX_OK);
  FXL_CHECK(sub_job.duplicate(kChildJobRights, limited_sub_job) == ZX_OK);

  set_job_policy(&sub_job, flg_create_process,
                        flg_call_trusty_api);
  return limited_sub_job;
}

zx::job* JobHolder::create_job_service(bool flg_create_process,
                                       bool flg_call_trusty_api) {
  zx::job sub_job;  // freed in end of function.
  zx::job* limited_sub_job = new zx::job();
  FXL_CHECK(zx::job::create(job_child_service_.get(), 0u, &sub_job) == ZX_OK);
  FXL_CHECK(sub_job.duplicate(kChildJobRights, limited_sub_job) == ZX_OK);

  set_job_policy(&sub_job, flg_create_process,
                        flg_call_trusty_api);
  return limited_sub_job;
}

zx::job* JobHolder::set_job_policy(zx::job* job,
                                   bool flg_create_process,
                                   bool flg_call_trusty_api) {
  if (!flg_create_process) {
    zx_policy_basic_t policy[] = {
        {ZX_POL_NEW_PROCESS, ZX_POL_ACTION_DENY},
    };
    zx_job_set_policy(job->get(), ZX_JOB_POL_ABSOLUTE, ZX_JOB_POL_BASIC,
                      policy, countof(policy));
  }

  if (!flg_call_trusty_api) {
    zx_policy_basic_t policy[] = {
        {ZX_POL_CALL_TRUSTY_API, ZX_POL_ACTION_DENY},
    };
    zx_job_set_policy(job->get(), ZX_JOB_POL_ABSOLUTE, ZX_JOB_POL_BASIC,
                      policy, countof(policy));
  }

  return job;
}

JobHolder::JobHolder(JobHolder* parent,
                     zx::channel host_directory,
                     fidl::StringPtr label)
    : parent_(parent),
      default_namespace_(
          fxl::MakeRefCounted<ApplicationNamespace>(nullptr, this, false, ::std::vector<::std::string>(), nullptr)),
      info_dir_(fbl::AdoptRef(new fs::PseudoDir())),
      info_vfs_(async_get_default()) {
  // parent_ is null if this is the root application environment. if so, we
  // derive from the application manager's job.
  zx_handle_t parent_job =
      parent_ != nullptr ? parent_->job_.get() : zx_job_default();
  FXL_CHECK(zx::job::create(parent_job, 0u, &job_) == ZX_OK);

#ifdef __Nebula__
  zx_policy_basic_t policy[] = {
        {ZX_POL_TRUSTY, ZX_POL_ACTION_DENY},
  };
  zx_policy_basic_t policy2[] = {
        {ZX_POL_NEW_PROCESS, ZX_POL_ACTION_DENY},
  };
  zx_job_set_policy(job_.get(), ZX_JOB_POL_ABSOLUTE, ZX_JOB_POL_BASIC, policy,
                      countof(policy));

  FXL_CHECK(zx::job::create(job_.get(), 0u, &job_for_child_normal_) == ZX_OK);
  FXL_CHECK(job_for_child_normal_.duplicate(kChildJobRights, &job_for_child_) == ZX_OK);

  fsl::SetObjectName(job_for_child_normal_.get(), "nbl-inner");
  zx_job_set_policy(job_for_child_normal_.get(), ZX_JOB_POL_ABSOLUTE, ZX_JOB_POL_BASIC, policy2,
                    countof(policy2));
#else
  FXL_CHECK(job_.duplicate(kChildJobRights, &job_for_child_) == ZX_OK);
#endif

  if (label->size() == 0)
    label_ = fxl::StringPrintf(kNumberedLabelFormat, next_numbered_label_++);
  else
    label_ = label.get().substr(0, component::kLabelMaxLength);

  fsl::SetObjectName(job_.get(), label_);

#ifdef __Nebula__
  if (label == "sys") {
    // Use parent job to create service and one-shot job.
    FXL_CHECK(zx::job::create(job_.get(), 0u, &job_service_) == ZX_OK);
    FXL_CHECK(job_service_.duplicate(kChildJobRights, &job_child_service_) ==
              ZX_OK);
    FXL_CHECK(zx::job::create(job_.get(), 0u, &job_one_shot_) == ZX_OK);
    FXL_CHECK(job_one_shot_.duplicate(kChildJobRights, &job_child_one_shot_) ==
              ZX_OK);

    fsl::SetObjectName(job_one_shot_.get(), "nbl-one-shot");
    fsl::SetObjectName(job_service_.get(), "nbl-service");

    trusty_add_mmio_check();
  }
#endif


  default_namespace_->services().set_backing_dir(std::move(host_directory));

  ServiceProviderPtr service_provider;
  default_namespace_->services().AddBinding(service_provider.NewRequest());
  loader_ = ConnectToService<ApplicationLoader>(service_provider.get());
}

JobHolder::~JobHolder() {
  job_.kill();
#ifdef __Nebula__
  job_for_child_normal_.kill();
  job_one_shot_.kill();
  job_service_.kill();
#endif
}

zx::channel JobHolder::OpenRootInfoDir() {
  JobHolder* root_job_holder = this;
  while (root_job_holder->parent() != nullptr) {
    root_job_holder = root_job_holder->parent();
  }

  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) < 0) {
    return zx::channel();
  }

  if (info_vfs_.ServeDirectory(root_job_holder->info_dir(), std::move(h1)) !=
      ZX_OK) {
    return zx::channel();
  }
  return h2;
}

void JobHolder::CreateNestedJob(
    zx::channel host_directory,
    fidl::InterfaceRequest<ApplicationEnvironment> environment,
    fidl::InterfaceRequest<ApplicationEnvironmentController> controller_request,
    fidl::StringPtr label) {
  auto controller = std::make_unique<ApplicationEnvironmentControllerImpl>(
      std::move(controller_request),
      std::make_unique<JobHolder>(this, std::move(host_directory), label));
  JobHolder* child = controller->job_holder();
  child->AddBinding(std::move(environment));
  info_dir_->AddEntry(child->label(), child->info_dir());
  children_.emplace(child, std::move(controller));

  PublishServicesForFirstNestedEnvironment(
      &child->default_namespace_->services());
}

void JobHolder::CreateApplication(
    ApplicationLaunchInfo launch_info,
    fidl::InterfaceRequest<ApplicationController> controller) {
  if (launch_info.url.get().empty()) {
    FXL_LOG(ERROR) << "Cannot create application because launch_info contains"
                      " an empty url";
    return;
  }
  std::string canon_url = CanonicalizeURL(launch_info.url);
  if (canon_url.empty()) {
    FXL_LOG(ERROR) << "Cannot run " << launch_info.url
                   << " because the url could not be canonicalized";
    return;
  }
  launch_info.url = canon_url;

  // launch_info is moved before LoadApplication() gets at its first argument.
  fidl::StringPtr url = launch_info.url;
  loader_->LoadApplication(
      url, fxl::MakeCopyable([this, launch_info = std::move(launch_info),
                              controller = std::move(controller)](
                                 ApplicationPackagePtr package) mutable {
        if (package) {
#ifdef __Nebula__
           // all trusty app can see "svc" folder, but use "white list services".
           const char* url = launch_info.url->c_str();
           bool has_whitelist_service = is_trusty_app(url);

           ::std::vector<::std::string> whitelist_services;
           if (has_whitelist_service) {
             bool need_tui = package->trusty_flags & NEBULA_APP_FLAGS_TUI;
             // If not find, it's false.
             if (need_tui) {
               whitelist_services.push_back(
                   ::std::string("droid.ActivityConnection"));
             }

             whitelist_services.push_back(std::string("nbl_services.rpc"));

#ifdef ENABLE_TRACER
          whitelist_services.push_back(
              ::std::string("trace_link.Registry"));
#endif
           }

           fxl::RefPtr<ApplicationNamespace> application_namespace =
               fxl::MakeRefCounted<ApplicationNamespace>(
                   default_namespace_, this, has_whitelist_service,
                   whitelist_services, std::move(launch_info.additional_services));
#else
           fxl::RefPtr<ApplicationNamespace> application_namespace =
               default_namespace_;
           if (launch_info.additional_services) {
             application_namespace = fxl::MakeRefCounted<ApplicationNamespace>(
                 default_namespace_, this,
                 std::move(launch_info.additional_services));
           }
#endif // __Nebula__
#ifdef __Nebula__
          if (package->err < 0) {
            GenNblAppToSendLaunchResponse(std::move(controller), package->err);
            return;
          }
#endif
          if (package->data) {
            std::string runner;
            LaunchType type = Classify(package->data->vmo, &runner);
            switch (type) {
              case LaunchType::kProcess:
                CreateApplicationWithProcess(
                    std::move(package), std::move(launch_info),
                    std::move(controller), std::move(application_namespace));
                break;
              case LaunchType::kArchive:
                CreateApplicationFromPackage(
                    std::move(package), std::move(launch_info),
                    std::move(controller), std::move(application_namespace));
                break;
            }
          } else if (package->directory) {
            CreateApplicationFromPackage(
                std::move(package), std::move(launch_info),
                std::move(controller), std::move(application_namespace));
          }
        }
#ifdef __Nebula__
        else {
          GenNblAppToSendLaunchResponse(std::move(controller), ZX_ERR_IO);
        }
#endif
      }));
}

std::unique_ptr<ApplicationEnvironmentControllerImpl> JobHolder::ExtractChild(
    JobHolder* child) {
  auto it = children_.find(child);
  if (it == children_.end()) {
    return nullptr;
  }
  auto controller = std::move(it->second);
  info_dir_->RemoveEntry(child->label());
  children_.erase(it);
  return controller;
}

std::unique_ptr<ApplicationControllerImpl> JobHolder::ExtractApplication(
    ApplicationControllerImpl* controller) {
  auto it = applications_.find(controller);
  if (it == applications_.end()) {
    return nullptr;
  }
  auto application = std::move(it->second);
  info_dir_->RemoveEntry(application->label());
  applications_.erase(it);
  return application;
}

#ifdef __Nebula__
std::unique_ptr<NblApplicationControllerImpl>
JobHolder::RemoveNblApplication(NblApplicationControllerImpl* controller) {
  auto it = nbl_applications_.find(controller);
  if (it == nbl_applications_.end()) {
    return nullptr;
  }
  auto application = std::move(it->second);
  nbl_applications_.erase(it);
  return application;
}

void JobHolder::GenNblAppToSendLaunchResponse(
    fidl::InterfaceRequest<ApplicationController> controller,
    int launch_res) {
  auto application = std::make_unique<NblApplicationControllerImpl>(
      std::move(controller), this, launch_res);
  NblApplicationControllerImpl* key = application.get();
  nbl_applications_.emplace(key, std::move(application));
}
#endif

void JobHolder::AddBinding(
    fidl::InterfaceRequest<ApplicationEnvironment> environment) {
  default_namespace_->AddBinding(std::move(environment));
}

void JobHolder::CreateApplicationWithProcess(
    ApplicationPackagePtr package,
    ApplicationLaunchInfo launch_info,
    fidl::InterfaceRequest<ApplicationController> controller,
    fxl::RefPtr<ApplicationNamespace> application_namespace) {
  NamespaceBuilder builder;
#if __Nebula__
  if (need_svc(launch_info.url->c_str())) {
    zx::channel svc = application_namespace->services().OpenAsDirectory();
    if (!svc)
      return;
    builder.AddServices(std::move(svc));
  }
#endif

  // Add the custom namespace.
  // Note that this must be the last |builder| step adding entries to the
  // namespace so that we can filter out entries already added in previous
  // steps.
  // HACK(alhaad): We add deprecated default directories after this.
  builder.AddFlatNamespace(std::move(launch_info.flat_namespace));
  // TODO(abarth): Remove this call to AddDeprecatedDefaultDirectories once
  // every application has a proper sandbox configuration.
  builder.AddDeprecatedDefaultDirectories();

#ifdef __Nebula__
  if (is_gnapp_loader(launch_info.url->c_str())) {
    builder.AddBootDirectory();
  }
  if (is_secmgr(launch_info.url->c_str())) {
    // For secmgr to notify devmgr via dmctl when new drivers installed in vfs.
    const std::string kMiscDev = "/dev/misc";
    fxl::UniqueFD dir(open(kMiscDev.c_str(), O_DIRECTORY | O_RDWR));
    FXL_CHECK(dir.is_valid());
    zx::channel handle = fsl::CloneChannelFromFileDescriptor(dir.get());
    FXL_CHECK(handle.is_valid());
    builder.AddDirectoryIfNotPresent(kMiscDev, std::move(handle));
  }
#endif

  fsl::SizedVmo executable;
  if (!fsl::SizedVmo::FromTransport(std::move(*package->data), &executable))
    return;

  const std::string url = launch_info.url;  // Keep a copy before moving it.
  auto channels = BindDirectory(&launch_info);

#ifdef __Nebula__
  int rc = ZX_OK;
  zx::process process =
      CreateProcess(this, std::move(executable), url, std::move(launch_info),
                    zx::channel(), builder.Build(), package.get(), &rc);
#else
  zx::process process = CreateProcess(job_for_child_, std::move(executable), url,
                            std::move(launch_info), zx::channel(),
                            builder.Build());
#endif

  if (process) {
    auto application = std::make_unique<ApplicationControllerImpl>(
        std::move(controller), this, nullptr, std::move(process), url,
        GetLabelFromURL(url), std::move(application_namespace),
        ExportedDirType::kPublicDebugCtrlLayout,
        std::move(channels.exported_dir), std::move(channels.client_request));
    ApplicationControllerImpl* key = application.get();
    info_dir_->AddEntry(application->label(), application->info_dir());
    applications_.emplace(key, std::move(application));
  }
#ifdef __Nebula__
  if (rc != ZX_OK) {
    GenNblAppToSendLaunchResponse(std::move(controller), rc);
  }
#endif
}

void JobHolder::CreateApplicationFromPackage(
    ApplicationPackagePtr package,
    ApplicationLaunchInfo launch_info,
    fidl::InterfaceRequest<ApplicationController> controller,
    fxl::RefPtr<ApplicationNamespace> application_namespace) {
  zx::channel pkg;
  std::unique_ptr<archive::FileSystem> pkg_fs;
  std::string sandbox_data;
  std::string runtime_data;
  ExportedDirType exported_dir_layout(ExportedDirType::kPublicDebugCtrlLayout);
  fsl::SizedVmo app_data;
  zx::channel loader_service;

  if (package->data) {
    pkg_fs =
        std::make_unique<archive::FileSystem>(std::move(package->data->vmo));
    pkg = pkg_fs->OpenAsDirectory();
    pkg_fs->GetFileAsString(kSandboxPath, &sandbox_data);
    if (!pkg_fs->GetFileAsString(kRuntimePath, &runtime_data))
      app_data = pkg_fs->GetFileAsVMO(kAppPath);
    exported_dir_layout = pkg_fs->IsFile(kLegacyFlatExportedDirPath)
                              ? ExportedDirType::kLegacyFlatLayout
                              : ExportedDirType::kPublicDebugCtrlLayout;
  } else if (package->directory) {
    fxl::UniqueFD fd =
        fsl::OpenChannelAsFileDescriptor(std::move(package->directory));
    files::ReadFileToStringAt(fd.get(), kSandboxPath, &sandbox_data);
    if (!files::ReadFileToStringAt(fd.get(), kRuntimePath, &runtime_data))
      VmoFromFilenameAt(fd.get(), kAppPath, &app_data);
    exported_dir_layout = files::IsFileAt(fd.get(), kLegacyFlatExportedDirPath)
                              ? ExportedDirType::kLegacyFlatLayout
                              : ExportedDirType::kPublicDebugCtrlLayout;
    // TODO(abarth): We shouldn't need to clone the channel here. Instead, we
    // should be able to tear down the file descriptor in a way that gives us
    // the channel back.
    pkg = fsl::CloneChannelFromFileDescriptor(fd.get());
    if (DynamicLibraryLoader::Start(std::move(fd), &loader_service) != ZX_OK)
      return;
  }
  if (!pkg)
    return;

  // Note that |builder| is only used in the else block below. It is left here
  // because we would like to use it everywhere once US-313 is fixed.
  NamespaceBuilder builder;
  builder.AddPackage(std::move(pkg));

#ifdef __Nebula__
  if (need_svc(launch_info.url->c_str())) {
    zx::channel svc = application_namespace->services().OpenAsDirectory();
    if (!svc)
      return;
    builder.AddServices(std::move(svc));
  }
#endif

  if (!sandbox_data.empty()) {
    SandboxMetadata sandbox;
    if (!sandbox.Parse(sandbox_data)) {
      FXL_LOG(ERROR) << "Failed to parse sandbox metadata for "
                     << launch_info.url;
      return;
    }

    // If an app has the "shell" feature, then we use the libraries from the
    // system rather than from the package because programs spawned from the
    // shell will need the system-provided libraries to run.
    if (sandbox.HasFeature("shell"))
      loader_service.reset();

    builder.AddSandbox(sandbox, [this] { return OpenRootInfoDir(); });
  }

  // Add the custom namespace.
  // Note that this must be the last |builder| step adding entries to the
  // namespace so that we can filter out entries already added in previous
  // steps.
  builder.AddFlatNamespace(std::move(launch_info.flat_namespace));

  const std::string url = launch_info.url;  // Keep a copy before moving it.
  if (app_data) {
    auto channels = BindDirectory(&launch_info);

#ifdef __Nebula__
    int rc = ZX_OK;
    zx::process process = CreateProcess(
        this, std::move(app_data), kAppArv0, std::move(launch_info),
        std::move(loader_service), builder.Build(), package.get(), &rc);
#else
    zx::process process = CreateProcess(
        job_for_child_, std::move(app_data), kAppArv0, std::move(launch_info),
        std::move(loader_service), builder.Build());
#endif
    if (process) {
      auto application = std::make_unique<ApplicationControllerImpl>(
          std::move(controller), this, std::move(pkg_fs), std::move(process),
          url, GetLabelFromURL(url), std::move(application_namespace),
          exported_dir_layout, std::move(channels.exported_dir),
          std::move(channels.client_request));
      ApplicationControllerImpl* key = application.get();
      info_dir_->AddEntry(application->label(), application->info_dir());
      applications_.emplace(key, std::move(application));
    }
#ifdef __Nebula__
    GenNblAppToSendLaunchResponse(std::move(controller), rc);
#endif
  } else {
    RuntimeMetadata runtime;
    if (!runtime.Parse(runtime_data)) {
      FXL_LOG(ERROR) << "Failed to parse runtime metadata for "
                     << launch_info.url;
      return;
    }

    ApplicationPackage inner_package;
    inner_package.resolved_url = package->resolved_url;

    ApplicationStartupInfo startup_info;
    startup_info.launch_info = std::move(launch_info);
    startup_info.flat_namespace = builder.BuildForRunner();

    auto* runner = GetOrCreateRunner(runtime.runner());
    if (runner == nullptr) {
      FXL_LOG(ERROR) << "Cannot create " << runner << " to run "
                     << launch_info.url;
      return;
    }
    runner->StartApplication(
        std::move(inner_package), std::move(startup_info), std::move(pkg_fs),
        std::move(application_namespace), std::move(controller));
  }
}

ApplicationRunnerHolder* JobHolder::GetOrCreateRunner(
    const std::string& runner) {
  // We create the entry in |runners_| before calling ourselves
  // recursively to detect cycles.
  auto result = runners_.emplace(runner, nullptr);
  if (result.second) {
    Services runner_services;
    ApplicationControllerPtr runner_controller;
    ApplicationLaunchInfo runner_launch_info;
    runner_launch_info.url = runner;
    runner_launch_info.directory_request = runner_services.NewRequest();
    CreateApplication(std::move(runner_launch_info),
                      runner_controller.NewRequest());

    runner_controller.set_error_handler(
        [this, runner] { runners_.erase(runner); });

    result.first->second = std::make_unique<ApplicationRunnerHolder>(
        std::move(runner_services), std::move(runner_controller));
  } else if (!result.first->second) {
    // There was a cycle in the runner graph.
    FXL_LOG(ERROR) << "Detected a cycle in the runner graph for " << runner
                   << ".";
    return nullptr;
  }

  return result.first->second.get();
}

}  // namespace component
