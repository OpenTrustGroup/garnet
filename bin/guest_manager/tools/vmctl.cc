// SPDX-License-Identifier: BSD-3-Clause

#include "garnet/bin/guest_manager/tools/serial.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>

#include <iostream>

#include <fuchsia/cpp/virtualization.h>

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/environment_services.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/log_settings.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/strings/string_printf.h"
#include "garnet/lib/machina/vm_id.h"
#include <zircon/vmid.h>

static virtualization::GuestManagerSyncPtr g_guest_manager;
constexpr uint32_t kDefaultHostVsockPort = 8888;

using virtualization::GuestError;

class SocatVsockAcceptor : public virtualization::HostVsockAcceptor {
 public:
  SocatVsockAcceptor(uint32_t port, async::Loop* loop)
      : host_port_(port), console_(loop) {}

  // |virtualization::HostVsockAcceptor|
  void Accept(uint32_t src_cid,
              uint32_t src_port,
              uint32_t port,
              AcceptCallback callback) override {
    if (port != host_port_) {
      std::cerr << "Unexpected port " << port << "\n";
      callback(ZX_ERR_CONNECTION_REFUSED, zx::handle());
      return;
    }
    zx::socket socket, remote_socket;
    zx_status_t status =
        zx::socket::create(ZX_SOCKET_STREAM, &socket, &remote_socket);
    if (status != ZX_OK) {
      std::cerr << "Failed to create socket " << status << "\n";
      callback(ZX_ERR_CONNECTION_REFUSED, zx::handle());
      return;
    }

    FXL_LOG(INFO) << "Accepted connection from guest cid: " << src_cid
                  << " port: " << src_port;
    callback(ZX_OK, std::move(remote_socket));
    console_.Start(zx::socket(std::move(socket)));
  }

 private:
  uint32_t host_port_;
  SerialConsole console_;
};

// clang-format off
void print_usage() {
  std::cout << "Usage: vmctl <subcommand> [options]\n\n"
               "subcommands:\n"
               "  info                   Show all VMs status\n"
               "  start [--vmid=vmid]    Start a VM\n"
               "  stop [--vmid=vmid]     Stop a running VM\n"
               "  shell [--vmid=vmid]    Attaching to VM's console\n"
               "  socat-listen [--vmid=vmid] [--host_port=port]\n"
               "                         Listen on host vsock port and forward to VM's console\n"
               "  set-param [--vmid=vmid] [--log_level=value]\n"
               "                         Set VM params in runtime\n"
               "\n"
               "default parameters:\n"
               "  vmid: " << machina::kSosVmid << " (SOS)\n"
               "  host_port: " << kDefaultHostVsockPort << "\n";
}
// clang-format on

int StartCommand(const fxl::CommandLine& command_line) {
  int8_t vmid = machina::kSosVmid;

  std::string vmid_str;
  if (command_line.GetOptionValue("vmid", &vmid_str)) {
    vmid = strtol(vmid_str.c_str(), nullptr, 10);
  }

  GuestError status;

  FXL_LOG(INFO) << "g_guest_manager->Launch"
                << fxl::StringPrintf(",Vmid: %d\n",
                                     zx_log_guest_vmid(vmid));
  bool success = g_guest_manager->Launch(vmid, &status);
  if (!success) {
    std::cerr << "Failed to communicate with guest manager" << std::endl;
    return 1;
  }

  if (status != GuestError::OK) {
    std::cerr << "Failed to start VM with "
              << fxl::StringPrintf(",Vmid: %d", zx_log_guest_vmid(vmid))
              << " ,ret= " << status
              << std::endl;
    return 1;
  }
  return 0;
}

int StopCommand(const fxl::CommandLine& command_line) {
  int8_t vmid = machina::kSosVmid;

  std::string vmid_str;
  if (command_line.GetOptionValue("vmid", &vmid_str)) {
    vmid = strtol(vmid_str.c_str(), nullptr, 10);
  }

  bool success = g_guest_manager->ForceShutdown(vmid);
  if (!success) {
    std::cerr << "Failed to communicate with guest manager" << std::endl;
    return 1;
  }

  return 0;
}

static zx_status_t ConnectToGuestController(
    virtualization::GuestControllerPtr& controller,
    async::Loop& loop,
    int8_t vmid) {
  GuestError status;

  FXL_LOG(INFO) << "g_guest_manager->Connect"
                << fxl::StringPrintf(",Vmid: %d\n",
                                     zx_log_guest_vmid(vmid));
  bool success =
      g_guest_manager->Connect(vmid, controller.NewRequest(), &status);
  if (!success) {
    std::cerr << "Failed to communicate with guest manager" << std::endl;
    return ZX_ERR_IO;
  }

  if (status != GuestError::OK) {
    std::cerr << "Failed to connect VM with"
              << fxl::StringPrintf(",Vmid: %d\n",
                                   zx_log_guest_vmid(vmid))
              << " ,ret= " << status
              << std::endl;
    return ZX_ERR_BAD_STATE;
  }

  controller.set_error_handler([&loop] {
    std::cerr << "VMM component is stopped\n";
    loop.Quit();
  });

  return ZX_OK;
}

int ShellCommand(async::Loop& loop, const fxl::CommandLine& command_line) {
  int8_t vmid = machina::kSosVmid;

  std::string vmid_str;
  if (command_line.GetOptionValue("vmid", &vmid_str)) {
    vmid = strtol(vmid_str.c_str(), nullptr, 10);
  }

  virtualization::GuestControllerPtr controller;
  zx_status_t status = ConnectToGuestController(controller, loop, vmid);
  if (status != ZX_OK) {
    return 1;
  }

  auto stop_cb = [&loop] { loop.Quit(); };
  InputReader reader(std::move(stop_cb));
  OutputWriter writer;

  FXL_LOG(INFO) << "controller->GetConsole";
  controller->GetConsole([&reader, &writer](zx::socket socket) {
    reader.Start(socket.get());
    writer.Start(std::move(socket));
  });

  loop.Run();
  return 0;
}

std::string guest_state_to_string(virtualization::GuestStatus status) {
  switch (status) {
    case virtualization::GuestStatus::NOT_STARTED:
      return "NOT_STARTED";
    case virtualization::GuestStatus::STOPPED:
      return "STOPPED";
    case virtualization::GuestStatus::STARTING:
      return "STARTING";
    case virtualization::GuestStatus::RUNNING:
      return "RUNNING";
    case virtualization::GuestStatus::STOPPING:
      return "STOPPING";
    case virtualization::GuestStatus::VMM_UNEXPECTED_TERMINATION:
      return "VMM_UNEXPECTED_TERMINATION";
    default:
      return "UNKNOWN";
  }
}

int InfoCommand(const fxl::CommandLine& command_line) {
  fidl::VectorPtr<virtualization::GuestInfo> infos;
  bool success = g_guest_manager->Show(&infos);
  if (!success) {
    std::cerr << "Failed to communicate with guest manager" << std::endl;
    return 1;
  }

  for (auto& info : *infos) {
    std::cout << fxl::StringPrintf(
        "Vmid: %d Status: %s, default_cfg: %s, bootloader: %s, log_level: %d\n",
        zx_log_guest_vmid(static_cast<int32_t>(info.config.vmid)),
        guest_state_to_string(info.status).c_str(),
        info.config.default_cfg->c_str(), info.config.bootloader->c_str(),
        info.config.log_level);
  }

  return 0;
}

int SocatListenCommand(async::Loop& loop,
                       const fxl::CommandLine& command_line) {
  int8_t vmid = machina::kSosVmid;
  uint32_t host_port = kDefaultHostVsockPort;

  std::string vmid_str;
  if (command_line.GetOptionValue("vmid", &vmid_str)) {
    vmid = strtol(vmid_str.c_str(), nullptr, 10);
  }

  std::string host_port_str;
  if (command_line.GetOptionValue("host_port", &host_port_str)) {
    host_port = strtol(host_port_str.c_str(), nullptr, 10);
  }

  virtualization::GuestControllerPtr controller;
  zx_status_t status = ConnectToGuestController(controller, loop, vmid);
  if (status != ZX_OK) {
    return 1;
  }

  virtualization::HostVsockEndpointSyncPtr vsock_endpoint;
  controller->GetHostVsockEndpoint(vsock_endpoint.NewRequest());

  SocatVsockAcceptor acceptor(host_port, &loop);
  fidl::Binding<virtualization::HostVsockAcceptor> binding(&acceptor);
  vsock_endpoint->Listen(host_port, binding.NewBinding(), &status);
  if (status != ZX_OK) {
    std::cerr << "Failed to listen on port " << host_port << "\n";
    return 1;
  }

  FXL_LOG(INFO) << "Listening on vsock port " << host_port
                << ", vmid: " << zx_log_guest_vmid(vmid);
  loop.Run();
  return 0;
}

int SetParamCommand(async::Loop& loop,
                    const fxl::CommandLine& command_line) {
  int8_t vmid = machina::kSosVmid;
  std::string vmid_str;
  if (command_line.GetOptionValue("vmid", &vmid_str)) {
    vmid = strtol(vmid_str.c_str(), nullptr, 10);
  }

  virtualization::GuestControllerPtr controller;
  zx_status_t status = ConnectToGuestController(controller, loop, vmid);
  if (status != ZX_OK) {
    return 1;
  }

  std::string log_level;
  if (command_line.GetOptionValue("log_level", &log_level)) {
    int32_t level = strtol(log_level.c_str(), nullptr, 10);
    controller->SetLogLevel(level);
    loop.RunUntilIdle();
  }

  return 0;
}

int HandleSubCommand(async::Loop& loop,
                     const std::string& subcommand,
                     const fxl::CommandLine& command_line) {
  if (subcommand == "start") {
    return StartCommand(command_line);
  } else if (subcommand == "stop") {
    return StopCommand(command_line);
  } else if (subcommand == "info") {
    return InfoCommand(command_line);
  } else if (subcommand == "shell") {
    return ShellCommand(loop, command_line);
  } else if (subcommand == "socat-listen") {
    return SocatListenCommand(loop, command_line);
  } else if (subcommand == "set-param") {
    return SetParamCommand(loop, command_line);
  } else {
    std::cerr << "Unknown subcommand: " << subcommand << std::endl;
    return 1;
  }
}

inline fxl::CommandLine CommandLineFromArgv(
    const std::vector<std::string>& argv_vec) {
  std::vector<const char*> argv;
  argv.reserve(argv_vec.size());
  for (const auto& s : argv_vec)
    argv.push_back(s.c_str());
  return fxl::CommandLineFromArgcArgv(static_cast<int>(argv.size()),
                                      argv.data());
}

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto app_context = component::ApplicationContext::CreateFromStartupInfo();
  app_context->ConnectToEnvironmentService(g_guest_manager.NewRequest());

  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  const auto& positional_args = command_line.positional_args();
  if (positional_args.empty()) {
    print_usage();
    return EXIT_FAILURE;
  }

  // Extract subcommand name
  std::string subcommand = positional_args[0];

  // Build new argv for the subcommand (strip off the subcommand itself)
  std::vector<std::string> sub_argv;
  sub_argv.push_back(argv[0]);  // program name
  for (size_t i = 1; i < positional_args.size(); ++i)
    sub_argv.push_back(positional_args[i]);
  for (const auto& option : command_line.options())
    sub_argv.push_back("--" + option.name +
                       (option.value.empty() ? "" : "=" + option.value));

  // Parse subcommand arguments separately
  fxl::CommandLine subcommand_line = CommandLineFromArgv(sub_argv);
  return HandleSubCommand(loop, subcommand, subcommand_line);
}
