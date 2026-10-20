// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2025 Goldenriver Inc. All rights reserved.
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "message_handler.h"

#include "garnet/bin/guest/proto/vm_message.pb.h"
#include "garnet/lib/machina/vhm_device.h"
#include "google/protobuf/text_format.h"
#include "lib/fsl/vmo/strings.h"

static nbl_vmm::Request get_message(mem::Buffer& rawbuf) {
  fsl::SizedVmo req_vmo;
  std::string req;
  FXL_CHECK(fsl::SizedVmo::FromTransport(std::move(rawbuf), &req_vmo));
  FXL_CHECK(fsl::StringFromVmo(req_vmo, &req));

  nbl_vmm::Request msg;
  FXL_CHECK(msg.ParseFromString(req));
  return msg;
}

void VmMessageHandler::OnNewCall(mem::Buffer reqbuf,
                                 OnNewCallCallback callback) {
  auto msg = get_message(reqbuf);
  std::string resp_buf;

  switch (msg.type()) {
    case nbl_vmm::RequestType::DUMP_VM: {
      std::vector<std::string> vcpu_states;
      guest_->Dump(vcpu_states);

      if (guest_->IsUosException()) {
        FXL_LOG(ERROR) << "VmMessageHandler  DUMP_VM,  uos exception, signal "
                          "kSignalVmDump";
        zx::event* dump_evnet = guest_->vm_dmp_event();
        dump_evnet->signal(0, machina::VhmDevice::kSignalVmDump);
      }

      nbl_vmm::Response resp;
      auto guest_state = resp.mutable_guest_state();
      for (auto& state : vcpu_states) {
        auto vcpu_state = guest_state->add_vcpus();
        auto success =
            google::protobuf::TextFormat::ParseFromString(state, vcpu_state);
        FXL_CHECK(success);
      }

      resp.SerializeToString(&resp_buf);

      break;
    }

    case nbl_vmm::RequestType::MAP_PHYSICAL_MEMORY: {
      auto cmd = msg.map_phys_mem();
      std::vector<uint64_t> page_list(cmd.page_list().begin(),
                                      cmd.page_list().end());
      guest_->UnmapPhysicalMemory(cmd.gpaddr(), page_list.size() * PAGE_SIZE);

      auto status =
          guest_->MapPhysicalMemory(cmd.gpaddr(), page_list, cmd.writeable());
      FXL_CHECK(status == ZX_OK);
      break;
    }

    case nbl_vmm::RequestType::UNMAP_PHYSICAL_MEMORY: {
      auto cmd = msg.unmap_phys_mem();
      guest_->UnmapPhysicalMemory(cmd.gpaddr(), cmd.length());
      break;
    }

    default:
      // TODO: should return fail
      FXL_CHECK(false);
  }

  fsl::SizedVmo resp_vmo;
  FXL_CHECK(fsl::VmoFromString(resp_buf, &resp_vmo));
  callback(std::move(resp_vmo).ToTransport());
}
