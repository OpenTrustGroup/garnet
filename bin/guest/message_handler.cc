#include "message_handler.h"

#include "garnet/bin/guest/proto/vm_message.pb.h"
#include "garnet/bin/guest/tracer.h"
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
  static nbl_vmm::Tracer tracer(ctx_);

  switch (msg.type()) {
    case nbl_vmm::RequestType::DUMP_VM: {
      std::vector<std::string> vcpu_states;
      guest_->Dump(vcpu_states);

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

    case nbl_vmm::RequestType::SHARED_IRQ_CTRL: {
      auto ctl = msg.shared_irq_ctrl();
      uint32_t vector = 0;
      uint32_t vmid = ctl.vmid();
      for (int index = 0; index < ctl.sharedirq_spec_size(); index ++) {
        auto spec = ctl.sharedirq_spec(index);
        if (vmid == spec.vmid()) {
          guest_->UpdateAfeIrq(spec.irq(), spec.status(), spec.clear(), spec.bitmask(), spec.vmid(), index);
          vector = spec.irq();
        }
      }
      guest_->UpdateAfeIrqRegs(vector);
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

    case nbl_vmm::RequestType::TRACE: {
      tracer.Start();
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
