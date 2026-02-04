// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/guest_config.h"

#include <libgen.h>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <fcntl.h>

#include <stack>
#include <tuple>
#include <utility>

#include <zircon/device/block.h>

#include "lib/fxl/files/file.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "third_party/rapidjson/rapidjson/document.h"

// 32 hex characters + 4 hyphens.
constexpr size_t kGuidStringLen = 36;
constexpr char kCpuFreqDev[] = "/dev/misc/cpufreq";

static void print_usage(fxl::CommandLine& cl) {
  // clang-format off
  std::cerr << "usage: " << cl.argv0() << " [OPTIONS]\n";
  std::cerr << "\n";
  std::cerr << "OPTIONS:\n";
  std::cerr << "\t--zircon=[kernel.bin]           Load a Zircon kernel from 'kernel.bin'\n";
  std::cerr << "\t--linux=[kernel.bin]            Load a Linux kernel from 'kernel.bin'\n";
  std::cerr << "\t--ramdisk=[ramdisk.bin]         Use file 'ramdisk.bin' as an initial RAM disk\n";
  std::cerr << "\t--cpus=[cpus]                   Number of virtual CPUs the guest is allowed to use\n";
  std::cerr << "\t--memory=[bytes]                Allocate 'bytes' of physical memory for the guest. The\n";
  std::cerr << "\t                                suffixes 'k', 'M', and 'G' are accepted (currently x64 only)\n";
  std::cerr << "\t--block=[block_spec]            Adds a block device with the given parameters\n";
  std::cerr << "\t--block-wait                    Wait for block devices (specified by GUID) to become\n";
  std::cerr << "\t                                available instead of failing.\n";
  std::cerr << "\t--cmdline=[cmdline]             Use string 'cmdline' as the kernel command line. This will\n";
  std::cerr << "\t                                overwrite any existing command line created using --cmdline\n";
  std::cerr << "\t                                or --cmdline-append.\n";
  std::cerr << "\t--cmdline-append=[cmdline]      Appends string 'cmdline' to the existing kernel command\n";
  std::cerr << "\t                                line\n";
  std::cerr << "\t--display={scenic,framebuffer,  Configures the display backend to use for the guest. 'scenic'\n";
  std::cerr << "\t           none}                (default) will render to a scenic view. 'framebuffer will draw\n";
  std::cerr << "\t                                to a zircon framebuffer. 'none' disables graphical output.\n";
  std::cerr << "\t--balloon-interval=[seconds]    Poll the virtio-balloon device every 'seconds' seconds\n";
  std::cerr << "\t                                and adjust the balloon size based on the amount of\n";
  std::cerr << "\t                                unused guest memory\n";
  std::cerr << "\t--balloon-threshold=[pages]     Number of unused pages to allow the guest to\n";
  std::cerr << "\t                                retain. Has no effect unless -m is also used\n";
  std::cerr << "\t--balloon-demand-page           Demand-page balloon deflate requests\n";
  std::cerr << "\t--gic                           Version 2 or 3\n";
  std::cerr << "\n";
  std::cerr << "BLOCK SPEC\n";
  std::cerr << "\n";
  std::cerr << " Block devices can be specified by path:\n";
  std::cerr << "    /pkg/data/disk.img\n";
  std::cerr << " Or by GPT Partition GUID:\n";
  std::cerr << "    guid:14db42cf-beb7-46a2-9ef8-89b13bb80528,rw\n";
  std::cerr << " Or by GPT Partition Type GUID:\n";
  std::cerr << "    type-guid:4F68BCE3-E8CD-4DB1-96E7-FBCAF984B709,rw\n";
  std::cerr << "\n";
  std::cerr << " Additional Options:\n";
  std::cerr << "    rw/ro: Create a read/write or read-only device.\n";
  std::cerr << "    fdio:  Use the FDIO back-end for the block device.\n";
  std::cerr << "\n";
  std::cerr << " Ex:\n";
  std::cerr << "\n";
  std::cerr << "  To open a filesystem resource packaged with the guest application\n";
  std::cerr << "  (read-only is important here as the /pkg/data namespace provides\n";
  std::cerr << "  read-only view into the package resources):\n";
  std::cerr << "\n";
  std::cerr << "      /pkg/data/system.img,fdio,ro\n";
  std::cerr << "\n";
  std::cerr << "  To specify a block device with a given path and read-write\n";
  std::cerr << "  permissions\n";
  std::cerr << "\n";
  std::cerr << "      /dev/class/block/000,fdio,rw\n";
  std::cerr << "\n";
  // clang-format on
}

static GuestConfigParser::OptionHandler save_option(std::string* out) {
  return [out](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value (--" << key
                     << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    *out = value;
    return ZX_OK;
  };
}

// A function that converts a string option into a custom type.
template <typename T>
using OptionTransform =
    std::function<zx_status_t(const std::string& arg, T* out)>;

// Handles and option by transforming the value and appending it to the
// given vector.
template <typename T>
static GuestConfigParser::OptionHandler append_option(
    std::vector<T>* out,
    OptionTransform<T> transform) {
  return [out, transform](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value (--" << key
                     << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    T t;
    zx_status_t status = transform(value, &t);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to parse option string '" << value << "'";
      return status;
    }
    out->push_back(t);
    return ZX_OK;
  };
}

static GuestConfigParser::OptionHandler append_string(std::string* out,
                                                      const char* delim) {
  return [out, delim](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value (--" << key
                     << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    out->append(delim);
    out->append(value);
    return ZX_OK;
  };
}

#if __x86_64__
constexpr size_t kMinMemorySize = 1 << 20;

static GuestConfigParser::OptionHandler parse_mem_size(size_t* out) {
  return [out](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value (--" << key
                     << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    char modifier = 'b';
    size_t size;
    int ret = sscanf(value.c_str(), "%zd%c", &size, &modifier);
    if (ret < 1) {
      FXL_LOG(ERROR) << "Value is not a size string: " << value;
      return ZX_ERR_INVALID_ARGS;
    }
    switch (modifier) {
      case 'b':
        break;
      case 'k':
        size *= (1 << 10);
        break;
      case 'M':
        size *= (1 << 20);
        break;
      case 'G':
        size *= (1 << 30);
        break;
      default:
        FXL_LOG(ERROR) << "Invalid size modifier " << modifier;
        return ZX_ERR_INVALID_ARGS;
    }

    if (size < kMinMemorySize) {
      FXL_LOG(ERROR) << "Requested memory " << size
                     << " is less than the minimum supported size "
                     << kMinMemorySize;
      return ZX_ERR_INVALID_ARGS;
    }
    *out = size;
    return ZX_OK;
  };
}
#endif

template <typename NumberType>
static GuestConfigParser::OptionHandler parse_number(NumberType* out) {
  return [out](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value (--" << key
                     << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    if (value.size() > 2 && value[0] == '0' &&
        (value[1] == 'x' || value[1] == 'X')) {
      if (!fxl::StringToNumberWithError(value.substr(2), out, fxl::Base::k16)) {
        FXL_LOG(ERROR) << "Unable to convert '" << value << "' into a number";
        return ZX_ERR_INVALID_ARGS;
      }
    } else {
      if (!fxl::StringToNumberWithError(value, out)) {
        FXL_LOG(ERROR) << "Unable to convert '" << value << "' into a number";
        return ZX_ERR_INVALID_ARGS;
      }
    }
    return ZX_OK;
  };
}

std::vector<std::string> split(const std::string& spec, char delim) {
  std::vector<std::string> tokens;
  std::string token;
  std::istringstream token_stream(spec);
  while (std::getline(token_stream, token, delim)) {
    tokens.push_back(token);
  }
  return tokens;
}

// Create an |OptionHandler| that sets |out| to a boolean flag. This can be
// specified not only as '--foo=true' or '--foo=false', but also as '--foo', in
// which case |out| will take the value of |default_flag_value|.
static GuestConfigParser::OptionHandler set_flag(bool* out,
                                                 bool default_flag_value) {
  return [out, default_flag_value](const std::string& key,
                                   const std::string& option_value) {
    bool flag_value = default_flag_value;
    if (!option_value.empty()) {
      if (option_value == "true") {
        flag_value = default_flag_value;
      } else if (option_value == "false") {
        flag_value = !default_flag_value;
      } else {
        FXL_LOG(ERROR) << "Option: '" << key
                       << "' expects either 'true' or 'false'; received '"
                       << option_value << "'";

        return ZX_ERR_INVALID_ARGS;
      }
    }
    *out = flag_value;
    return ZX_OK;
  };
}

static zx_status_t parse_guid(const std::string& guid_str,
                              machina::BlockDispatcher::Guid& guid) {
  if (!guid.empty()) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (guid_str.size() != kGuidStringLen) {
    return ZX_ERR_INVALID_ARGS;
  }

  int ret = sscanf(
      guid_str.c_str(),
      "%2hhx%2hhx%2hhx%2hhx-%2hhx%2hhx-%2hhx%2hhx-%2hhx%2hhx-%2hhx%2hhx%2hhx%"
      "2hhx%2hhx%2hhx",
      &guid.bytes[3], &guid.bytes[2], &guid.bytes[1], &guid.bytes[0],
      &guid.bytes[5], &guid.bytes[4], &guid.bytes[7], &guid.bytes[6],
      &guid.bytes[8], &guid.bytes[9], &guid.bytes[10], &guid.bytes[11],
      &guid.bytes[12], &guid.bytes[13], &guid.bytes[14], &guid.bytes[15]);
  return (ret == 16) ? ZX_OK : ZX_ERR_INVALID_ARGS;
}

static zx_status_t parse_block_spec(const std::string& spec,
                                    machina::BlockSpec* out) {
  std::string token;
  std::istringstream tokenStream(spec);
  while (std::getline(tokenStream, token, ',')) {
    if (token == "fdio") {
      out->data_plane = machina::BlockDispatcher::DataPlane::FDIO;
    } else if (token == "rw") {
      out->mode = machina::BlockDispatcher::Mode::RW;
    } else if (token == "ro") {
      out->mode = machina::BlockDispatcher::Mode::RO;
    } else if (token.size() > 0 && token[0] == '/') {
      out->path = std::move(token);
    } else if (token.compare(0, 5, "guid:") == 0) {
      std::string guid_str = token.substr(5);
      zx_status_t status = parse_guid(guid_str, out->guid);
      if (status != ZX_OK) {
        return status;
      }
      out->guid.type = machina::BlockDispatcher::GuidType::GPT_PARTITION_GUID;
    } else if (token.compare(0, 10, "type-guid:") == 0) {
      std::string guid_str = token.substr(10);
      zx_status_t status = parse_guid(guid_str, out->guid);
      if (status != ZX_OK) {
        return status;
      }
      out->guid.type =
          machina::BlockDispatcher::GuidType::GPT_PARTITION_TYPE_GUID;
    } else if (token == "volatile") {
      out->volatile_writes = true;
    }
  }

  // Path and GUID are mutually exclusive, but one must be provided.
  if (out->path.empty() == out->guid.empty()) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

static zx_status_t parse_shared_irq_spec(const std::string& spec,
                                         machina::SharedIrqSpec* out) {
  int ret = sscanf(spec.c_str(), "%d,0x%lx,0x%x,0x%x,0x%lx", &out->vector,
                   &out->ctrl_reg_base, &out->irq_status_offset,
                   &out->irq_clear_offset, &out->interested_bitmask);
  if (ret != 5) {
    FXL_LOG(ERROR) << "Invalid shared-irq string: " << spec;
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

static zx_status_t parse_audio_irq_spec(const std::string& spec,
                                         machina::SharedIrqSpec* out) {
  int ret = sscanf(spec.c_str(), "%d,0x%lx,0x%x,0x%x,0x%lx", &out->vector,
                   &out->ctrl_reg_base, &out->irq_status_offset,
                   &out->irq_clear_offset, &out->interested_bitmask);
  if (ret != 5) {
    FXL_LOG(ERROR) << "Invalid audio-irq string: " << spec;
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

static GuestConfigParser::OptionHandler parse_ipc_mbox_spec(
    std::vector<machina::IpcMboxSpec> *out) {
  return [out](const std::string& key, const std::string& value) {
    machina::IpcMboxSpec ipcmbox;
	int ret =
		sscanf(value.c_str(), "%hd,%hd", &ipcmbox.rx_ready_irq, &ipcmbox.tx_done_irq);
	if (ret != 2) {
		FXL_LOG(ERROR) << "Invalid ipc-mbox string: " << value;
		return ZX_ERR_INVALID_ARGS;
	}
	out->push_back(std::move(ipcmbox));
    return ZX_OK;
  };
}

static GuestConfigParser::OptionHandler parse_cpufreq(
    std::string *out) {
  return [out](const std::string& key, const std::string& value) {
    int fd = open(kCpuFreqDev, O_WRONLY);
    if (fd < 0) {
      FXL_LOG(ERROR) << "Failed to open " << kCpuFreqDev << ": errno=" << errno;
    }
    fdio_ioctl(fd, 0, value.c_str(), value.length(), NULL, 0);
    close(fd);
    *out = value;
    return ZX_OK;
  };
}

static GuestConfigParser::OptionHandler save_kernel(
    std::string* out,
    machina::Kernel* kernel,
    machina::Kernel set_kernel) {
  return [out, kernel, set_kernel](const std::string& key,
                                   const std::string& value) {
    zx_status_t status = save_option(out)(key, value);
    if (status == ZX_OK) {
      *kernel = set_kernel;
    }
    return status;
  };
}

static GuestConfigParser::OptionHandler parse_display(
    machina::GuestDisplay* out) {
  return [out](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value (--" << key
                     << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    if (value == "scenic") {
      *out = machina::GuestDisplay::SCENIC;
    } else if (value == "framebuffer") {
      *out = machina::GuestDisplay::FRAMEBUFFER;
    } else if (value == "none") {
      *out = machina::GuestDisplay::NONE;
    } else {
      FXL_LOG(ERROR) << "Invalid display value: " << value;
      return ZX_ERR_INVALID_ARGS;
    }
    return ZX_OK;
  };
}

static GuestConfigParser::OptionHandler parse_gic(machina::Gic* out) {
  return [out](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value (--" << key
                     << "=<value>)";
      return ZX_ERR_INVALID_ARGS;
    }
    uint32_t number;
    if (!fxl::StringToNumberWithError<uint32_t>(value, &number)) {
      FXL_LOG(ERROR) << "Unable to convert '" << value << "' into a number";
      return ZX_ERR_INVALID_ARGS;
    }
    if (number == 2) {
      *out = machina::Gic::V2;
    } else if (number == 3) {
      *out = machina::Gic::V3;
    } else {
      FXL_LOG(ERROR) << "Invalid GIC version";
      return ZX_ERR_INVALID_ARGS;
    }
    return ZX_OK;
  };
}

static GuestConfigParser::OptionHandler parse_vcpu_regs(
    uint64_t (&out)[machina::kNumVcpuRegs]) {
  return [&out](const std::string& key, const std::string& value) {
    uint64_t regidx, regval;
    int ret = sscanf(value.c_str(), "%lu,%li", &regidx, &regval);
    if (ret != 2) {
      FXL_LOG(ERROR) << "Invalid register string: " << value;
      return ZX_ERR_INVALID_ARGS;
    }

    if (regidx >= machina::kNumVcpuRegs) {
      FXL_LOG(ERROR) << "Invalid register index: " << regidx;
      return ZX_ERR_OUT_OF_RANGE;
    }

    out[regidx] = regval;
    return ZX_OK;
  };
}

static GuestConfigParser::OptionHandler parse_vgic_irq(
    std::vector<uint16_t>* out) {
  return [out](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value";
      return ZX_ERR_INVALID_ARGS;
    }

    uint32_t hwirq_start, hwirq_end;
    int ret = sscanf(value.c_str(), "%i-%i", &hwirq_start, &hwirq_end);
    if (ret != 2) {
      int ret = sscanf(value.c_str(), "%i", &hwirq_start);
      if (ret != 1) {
        FXL_LOG(ERROR) << "Invalid irq string: " << value;
        return ZX_ERR_INVALID_ARGS;
      }
      hwirq_end = hwirq_start;
    }

    if (hwirq_end < hwirq_start) {
      FXL_LOG(ERROR) << "irq_end should be greater or equal to irq_start";
      return ZX_ERR_INVALID_ARGS;
    }

    for (uint32_t hwirq = hwirq_start; hwirq <= hwirq_end; hwirq++) {
      out->push_back(hwirq);
    }
    return ZX_OK;
  };
}


static GuestConfigParser::OptionHandler parse_smmu_sids(std::vector<uint16_t> *out) {
  return [out](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value";
      return ZX_ERR_INVALID_ARGS;
    }

    uint32_t sid_start,sid_end;
    int ret = sscanf(value.c_str(), "%i-%i", &sid_start, &sid_end);

    if (ret != 2) {
      int ret = sscanf(value.c_str(), "%i", &sid_start);
      if(ret != 1){
        FXL_LOG(ERROR) << "Invalid sid string: " << value;
        return ZX_ERR_INVALID_ARGS;
      }
      sid_end = sid_start;
    }
    if (sid_end < sid_start) {
      FXL_LOG(ERROR) << "sid_end should be greater or equal to sid_start";
      return ZX_ERR_INVALID_ARGS;
    }
    for (uint32_t sid = sid_start; sid <= sid_end; sid++) {
      out->push_back(sid);
    }

    return ZX_OK;
  };
}

static GuestConfigParser::OptionHandler parse_irq_monitor_irqs(
        std::vector<uint16_t> *out) {
  return [out](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value";
      return ZX_ERR_INVALID_ARGS;
    }

    uint32_t irq_start, irq_end;
    int ret = sscanf(value.c_str(), "%i-%i", &irq_start, &irq_end);
    if (ret != 2) {
      int ret = sscanf(value.c_str(), "%i", &irq_start);
      if (ret != 1) {
        FXL_LOG(ERROR) << "Invalid irq string: " << value;
        return ZX_ERR_INVALID_ARGS;
      }
      irq_end = irq_start;
    }

    if (irq_end < irq_start) {
      FXL_LOG(ERROR) << "irq_end should be greater or equal to irq_start";
      return ZX_ERR_INVALID_ARGS;
    }

    for (uint32_t irq = irq_start; irq <= irq_end; irq++) {
      out->push_back(irq);
    }
    return ZX_OK;
  };
}

static GuestConfigParser::OptionHandler parse_vmem(
    std::vector<machina::VmemSpec>* out) {
  return [out](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value";
      return ZX_ERR_INVALID_ARGS;
    }

    machina::VmemSpec spec;
    int ret =
        sscanf(value.c_str(), "%li,%li,%li,%hhu,%hhu", &spec.gpa_base,
               &spec.hpa_base, &spec.size, &spec.policy, &spec.is_physmem);
    if (ret != 5) {
      FXL_LOG(ERROR) << "Invalid vmem string: " << value;
      return ZX_ERR_INVALID_ARGS;
    }

    if (spec.gpa_base & (PAGE_SIZE - 1)) {
      FXL_LOG(ERROR) << "Unaligned vmem gpa_base: " << spec.gpa_base;
      return ZX_ERR_INVALID_ARGS;
    }

    if (spec.hpa_base & (PAGE_SIZE - 1)) {
      FXL_LOG(ERROR) << "Unaligned vmem hpa_base: " << spec.hpa_base;
      return ZX_ERR_INVALID_ARGS;
    }

    if (spec.size & (PAGE_SIZE - 1)) {
      FXL_LOG(ERROR) << "Unaligned vmem size: " << spec.size;
      return ZX_ERR_INVALID_ARGS;
    }

    if (spec.policy >= machina::MemoryPolicy::kMax) {
      FXL_LOG(ERROR) << "Invalid vmem policy: "
                     << static_cast<uint32_t>(spec.policy);
      return ZX_ERR_INVALID_ARGS;
    }

    out->push_back(std::move(spec));
    return ZX_OK;
  };
}
GuestConfigParser::OptionMap GuestConfigParser::GetVsmmuOptionHandlers(machina::VsmmuSpec& spec) {
  GuestConfigParser::OptionMap handlers;
  handlers.emplace("paddr", parse_number(&spec.paddr));
  handlers.emplace("interrupt", parse_number(&spec.irq));
  handlers.emplace("sids", parse_smmu_sids(&spec.sids));
  return handlers;
}

static GuestConfigParser::OptionHandler parse_device_tree(
    machina::DeviceTreeSpec* out) {
  return [out](const std::string& key, const std::string& value) {
    int ret = sscanf(value.c_str(), "0x%lx,0x%lx", &out->base, &out->size);
    if (ret != 2) {
      FXL_LOG(ERROR) << "Invalid device tree string: " << value;
      return ZX_ERR_INVALID_ARGS;
    }

    return ZX_OK;
  };
}

static GuestConfigParser::OptionHandler parse_irq(uint32_t* out) {
  return [out](const std::string& key, const std::string& value) {
    if (!fxl::StringToNumberWithError<uint32_t>(value, out)) {
      FXL_LOG(ERROR) << "Unable to convert '" << value << "' into a number";
      return ZX_ERR_INVALID_ARGS;
    }

    return ZX_OK;
  };
}

static GuestConfigParser::OptionHandler parse_bind_physical_cpu(std::vector<uint32_t> *out) {
  return [out](const std::string& key, const std::string& value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value";
      return ZX_ERR_INVALID_ARGS;
    }

    std::stringstream to_split(value);
    std::string segment;
    uint32_t cpu;
    while (std::getline(to_split, segment, ',')) {
      std::istringstream(segment) >> cpu;
      out->push_back(cpu);
    }

    return ZX_OK;
  };
}

static GuestConfigParser::OptionHandler parse_array_u64(
    std::vector<uint64_t> *out) {
  return [out](const std::string &key, const std::string &value) {
    if (value.empty()) {
      FXL_LOG(ERROR) << "Option: '" << key << "' expects a value";
      return ZX_ERR_INVALID_ARGS;
    }

    std::stringstream to_split(value);
    std::string segment;
    uint64_t budget;
    while (std::getline(to_split, segment, ',')) {
      std::istringstream(segment) >> budget;
      out->push_back(budget);
    }

    return ZX_OK;
  };
}

GuestConfigParser::OptionMap GuestConfigParser::GetRootOptionHandlers() {
  GuestConfigParser::OptionMap handlers;
  handlers.emplace("zircon", save_kernel(&cfg_->kernel_path_, &cfg_->kernel_,
                                         machina::Kernel::ZIRCON));
  handlers.emplace("linux", save_kernel(&cfg_->kernel_path_, &cfg_->kernel_,
                                        machina::Kernel::LINUX));
  handlers.emplace("ramdisk", save_option(&cfg_->ramdisk_path_));
  handlers.emplace("block", append_option<machina::BlockSpec>(
                                &cfg_->block_specs_, parse_block_spec));
  handlers.emplace("cmdline", save_option(&cfg_->cmdline_));
  handlers.emplace("cmdline-append", append_string(&cfg_->cmdline_, " "));
  handlers.emplace("cpus", parse_number(&cfg_->num_cpus_));
#if __x86_64__
  handlers.emplace("memory", parse_mem_size(&cfg_->memory_));
#endif
  handlers.emplace("balloon-demand-page",
                   set_flag(&cfg_->balloon_demand_page_, true));
  handlers.emplace("balloon-interval",
                   parse_number(&cfg_->balloon_interval_seconds_));
  handlers.emplace("balloon-threshold",
                   parse_number(&cfg_->balloon_pages_threshold_));
  handlers.emplace("display", parse_display(&cfg_->display_));
  handlers.emplace("block-wait", set_flag(&cfg_->block_wait_, true));
  handlers.emplace("gic", parse_gic(&cfg_->gic_version_));
  handlers.emplace("vmem", parse_vmem(&cfg_->vmem_spec_));
  handlers.emplace("device-tree", parse_device_tree(&cfg_->dtb_spec_));
  handlers.emplace("tipc-vq-notifier-irq",
                   parse_irq(&cfg_->tipc_vq_notifier_irq_));
  handlers.emplace("vhm-irq", parse_irq(&cfg_->vhm_irq_));
  handlers.emplace("guest-reserved-memory",
                   parse_number(&cfg_->guest_reserved_memory_));
  handlers.emplace("physical-cpus",
                   parse_bind_physical_cpu(&cfg_->bind_pcpus_));
  handlers.emplace("periods", parse_array_u64(&cfg_->periods_));
  handlers.emplace("budgets", parse_array_u64(&cfg_->budgets_));
  handlers.emplace("sched-priority", parse_array_u64(&cfg_->sched_priority_));
  handlers.emplace("sched-timeslice", parse_array_u64(&cfg_->sched_timeslice_));
  handlers.emplace("shared-irqs",
                   append_option<machina::SharedIrqSpec>(
                       &cfg_->vgic_spec_.shared_irqs, parse_shared_irq_spec));
  handlers.emplace("audio-irqs",
                   append_option<machina::SharedIrqSpec>(
                       &cfg_->vgic_spec_.audio_irqs, parse_audio_irq_spec));
  handlers.emplace("ipc-mbox", parse_ipc_mbox_spec(&cfg_->mbox_spec_));
  handlers.emplace("sched-irq", parse_number(&cfg_->sched_irq_));
  handlers.emplace("dump-irq", parse_number(&cfg_->dump_irq_));
  handlers.emplace("sched-id", parse_number(&cfg_->sched_id_));
  handlers.emplace("cpufreq", parse_cpufreq(&cfg_->cpufreq_));
  handlers.emplace("gpu-irq", parse_irq(&cfg_->gpu_irq_));
  handlers.emplace("vsock-irqs", parse_vgic_irq(&cfg_->vsock_irqs_));
  handlers.emplace("apu-irqs", parse_vgic_irq(&cfg_->apu_irqs_));
  handlers.emplace("cmdq-irqs", parse_vgic_irq(&cfg_->cmdq_irqs_));
  handlers.emplace("vtee-notifier-irq",
                   parse_irq(&cfg_->vtee_notifier_irq_));
  return handlers;
}

GuestConfigParser::OptionMap GuestConfigParser::GetVcpuOptionHandlers() {
  GuestConfigParser::OptionMap handlers;
  // TODO(TC): MaxCpuNum, HCR, Entry sanity check
  handlers.emplace("entry", parse_number(&cfg_->vcpu_spec_.entry));
  handlers.emplace("x", parse_vcpu_regs(cfg_->vcpu_spec_.x));
  return handlers;
}

GuestConfigParser::OptionMap GuestConfigParser::GetVgicOptionHandlers() {
  GuestConfigParser::OptionMap handlers;
  handlers.emplace("gicd_paddr", parse_number(&cfg_->vgic_spec_.gicd_paddr));
  handlers.emplace("gicr_paddr", parse_number(&cfg_->vgic_spec_.gicr_paddr));
  handlers.emplace("interrupt", parse_vgic_irq(&cfg_->vgic_spec_.irqs));
  handlers.emplace("percpu_interrupt", parse_vgic_irq(&cfg_->vgic_spec_.percpu_irqs));
  return handlers;
}

GuestConfigParser::OptionMap GuestConfigParser::GetRprocOptionHandlers() {
  GuestConfigParser::OptionMap handlers;
  handlers.emplace("has_ipi_affinity", set_flag(&cfg_->rproc_spec_.has_ipi_affinity, true));
  handlers.emplace("ctrl_irq", parse_number(&cfg_->rproc_spec_.ctrl_irq));
  handlers.emplace("local_irqs", parse_vgic_irq(&cfg_->rproc_spec_.local_irqs));
  handlers.emplace("remote_irqs", parse_vgic_irq(&cfg_->rproc_spec_.remote_irqs));
  return handlers;
}

GuestConfigParser::OptionMap GuestConfigParser::GetIrqMonitorOptionHandlers() {
  GuestConfigParser::OptionMap handlers;
  handlers.emplace("enable", parse_number(&cfg_->irq_monitor_spec_.enable));
  handlers.emplace("irq-count-threshold",
                   parse_number(&cfg_->irq_monitor_spec_.threshold));
  handlers.emplace("irqs", parse_irq_monitor_irqs(&cfg_->irq_monitor_spec_.irqs));
  return handlers;
}

GuestConfigParser::OptionMap GuestConfigParser::GetOptionHandlers(
    const std::string& name) {
  if (name == kRootOptionName)
    return GetRootOptionHandlers();
  if (name == kVcpuOptionName)
    return GetVcpuOptionHandlers();
  if (name == kVgicOptionName)
    return GetVgicOptionHandlers();
  if (name == kIrqMonitorOptionName)
    return GetIrqMonitorOptionHandlers();

  if (name == kRprocOptionName)
    return GetRprocOptionHandlers();
// Assuming kVsmmuOptionName represents the key for vsmmu objects
  if (name.rfind(kVsmmuptionName, 0) == 0 ) {
      // Only create a new VsmmuSpec when starting to parse a new vsmmu object
      if (current_vsmmu_spec_index_ == -1 || name != current_vsmmu_name_) {
        auto& vsmmus = cfg_->vsmmus();
        vsmmus.emplace_back();  // add new VsmmuSpec object
        current_vsmmu_spec_index_ = vsmmus.size() - 1;
        current_vsmmu_name_ = name;
      }
      return GetVsmmuOptionHandlers(cfg_->vsmmus().back());
    }
  return {};
}

GuestConfigParser::GuestConfigParser(GuestConfig* cfg) : cfg_(cfg) , current_vsmmu_spec_index_(-1){}

GuestConfigParser::~GuestConfigParser() = default;

zx_status_t GuestConfigParser::ParseArgcArgv(int argc, char** argv) {
  fxl::CommandLine cl = fxl::CommandLineFromArgcArgv(argc, argv);

  if (cl.positional_args().size() > 0) {
    FXL_LOG(ERROR) << "Unknown positional option: " << cl.positional_args()[0];
    print_usage(cl);
    return ZX_ERR_INVALID_ARGS;
  }

  auto opts = GetRootOptionHandlers();
  for (const fxl::CommandLine::Option& option : cl.options()) {
    auto entry = opts.find(option.name);
    if (entry == opts.end()) {
      FXL_LOG(ERROR) << "Unknown option --" << option.name;
      print_usage(cl);
      return ZX_ERR_INVALID_ARGS;
    }
    zx_status_t status = entry->second(option.name, option.value);
    if (status != ZX_OK) {
      print_usage(cl);
      return ZX_ERR_INVALID_ARGS;
    }
  }

  return ZX_OK;
}

zx_status_t GuestConfigParser::ParseProductConfig(const std::string& data) {
  rapidjson::Document doc;
  doc.Parse(data);
  if (!doc.IsObject()) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (doc.HasMember("device-tree") && doc["device-tree"].IsString()) {
    machina::DeviceTreeSpec dtb;
    const std::string dtbStr = doc["device-tree"].GetString();
	int ret = sscanf(dtbStr.c_str(), "%lx,%lx", &dtb.base, &dtb.size);
	if (ret != 2) {
		FXL_LOG(ERROR) << "Invalid device-tree string: " << doc["device-tree"].GetString();
		return ZX_ERR_INVALID_ARGS;
	}
    cfg_->dtb_spec_ = fbl::move(dtb);
  }

  if (doc.HasMember("vmem") && doc["vmem"].IsArray()) {
    const rapidjson::Value& vmemArray = doc["vmem"];
    for(rapidjson::SizeType i = 0; i < vmemArray.Size(); i++) {
      machina::VmemSpec spec;
      const std::string vmemStr = vmemArray[i].GetString();
      char namebuf[256];

    int ret = sscanf(vmemStr.c_str(), "%255[^,],%li,%li,%li,%hhu,%hhu",
                     namebuf, &spec.gpa_base, &spec.hpa_base, &spec.size, &spec.policy, &spec.is_physmem);
      spec.name = namebuf;
      if (ret != 6) {
        FXL_LOG(ERROR) << "Invalid vmem string: " << vmemStr;
        return ZX_ERR_INVALID_ARGS;
      }

      if (spec.gpa_base & (PAGE_SIZE - 1)) {
        FXL_LOG(ERROR) << "Unaligned vmem gpa_base: " << spec.gpa_base;
        return ZX_ERR_INVALID_ARGS;
      }

      if (spec.hpa_base & (PAGE_SIZE - 1)) {
        FXL_LOG(ERROR) << "Unaligned vmem hpa_base: " << spec.hpa_base;
        return ZX_ERR_INVALID_ARGS;
      }

      if (spec.size & (PAGE_SIZE - 1)) {
        FXL_LOG(ERROR) << "Unaligned vmem size: " << spec.size;
        return ZX_ERR_INVALID_ARGS;
      }

      if (spec.policy >= machina::MemoryPolicy::kMax) {
        FXL_LOG(ERROR) << "Invalid vmem policy: "
                     << static_cast<uint32_t>(spec.policy);
        return ZX_ERR_INVALID_ARGS;
      }
      cfg_->vmem_spec_.push_back(spec);
    }
  }

  if (doc.HasMember("product") && doc["product"].IsArray()) {
    const rapidjson::Value& productArray = doc["product"];
    for(rapidjson::SizeType i = 0; i < productArray.Size(); i++) {
      const rapidjson::Value& item = productArray[i];
      machina::ProductSpec pobj;

      if (item.HasMember("name") && item["name"].IsString()) {
        pobj.name = item["name"].GetString();
      } else {
        return ZX_ERR_INVALID_ARGS;
      }

      if (item.HasMember("file") && item["file"].IsString()) {
        pobj.file = item["file"].GetString();
      } else {
        return ZX_ERR_INVALID_ARGS;
      }

      cfg_->product_spec_.push_back(pobj);
    }
  }

  return ZX_OK;
}

zx_status_t GuestConfigParser::ParseConfig(const std::string& data) {
  rapidjson::Document document;
  document.Parse(data);
  if (!document.IsObject()) {
    return ZX_ERR_INVALID_ARGS;
  }

  using MemberIter = rapidjson::Value::ConstMemberIterator;
  std::stack<std::tuple<MemberIter, MemberIter, std::string>> stack;
  stack.push({document.MemberBegin(), document.MemberEnd(), kRootOptionName});

  while (!stack.empty()) {
    auto& iter_begin = std::get<0>(stack.top());
    auto& iter_end = std::get<1>(stack.top());
    auto& opts_name = std::get<2>(stack.top());
    if (iter_begin == iter_end) {
      stack.pop();
      continue;
    }
    auto& member = *iter_begin++;

    // For nested object members, depth-first-search on them firstly.
    if (member.value.IsObject()) {
      stack.push({member.value.GetObject().MemberBegin(),
                  member.value.GetObject().MemberEnd(),
                  member.name.GetString()});
      continue;
    }

    auto opts = GetOptionHandlers(opts_name);
    auto entry = opts.find(member.name.GetString());
    if (entry == opts.end()) {
      FXL_LOG(ERROR) << "Unknown field in configuration object: "
                     << member.name.GetString();
      return ZX_ERR_INVALID_ARGS;
    }

    // For string members, invoke the handler directly on the value.
    if (member.value.IsString()) {
      zx_status_t status =
          entry->second(member.name.GetString(), member.value.GetString());
      if (status != ZX_OK) {
        return ZX_ERR_INVALID_ARGS;
      }
      continue;
    }

    // For array members, invoke the handler on each value in the array.
    if (member.value.IsArray()) {
      for (auto& array_member : member.value.GetArray()) {
        if (array_member.IsString()) {
          zx_status_t status =
            entry->second(member.name.GetString(), array_member.GetString());
          if (status != ZX_OK) {
            return ZX_ERR_INVALID_ARGS;
          }
        } else {
          FXL_LOG(ERROR) << "Array entry has incorect type, expected string: "
                         << member.name.GetString();
          return ZX_ERR_INVALID_ARGS;
        }
      }
      continue;
    }
    FXL_LOG(ERROR) << "Field has incorrect type, expected string or array: "
                   << member.name.GetString();
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

uint64_t GuestConfig::phys_base() const {
  for (const machina::VmemSpec& mem : vmem_spec_) {
    if (mem.is_physmem) {
      return mem.gpa_base;
    }
  }
  __builtin_unreachable();
}

uint64_t GuestConfig::phys_size() const {
  for (const machina::VmemSpec& mem : vmem_spec_) {
    if (mem.is_physmem) {
      return mem.size;
    }
  }
  __builtin_unreachable();
}
