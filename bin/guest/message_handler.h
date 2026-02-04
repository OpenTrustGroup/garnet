#pragma once

#include <fuchsia/cpp/machina.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fsl/vmo/sized_vmo.h>

#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/guest_config.h"
#include "garnet/lib/machina/interrupt_controller.h"
#include "garnet/lib/machina/io.h"
#include "garnet/lib/machina/phys_mem.h"
#include "garnet/lib/machina/audio_irq_client.h"

class VmMessageHandler : public machina::MessageListener {
 public:
  VmMessageHandler(machina::Guest* guest, component::ApplicationContext* ctx)
      : guest_(guest), binding_(this), ctx_(ctx) {
    FXL_CHECK(loop_.StartThread() == ZX_OK);
  }

  fidl::InterfaceHandle<machina::MessageListener> NewBinding(void) {
    return binding_.NewBinding(loop_.async());
  }

  virtual void OnNewCall(mem::Buffer rawbuf,
                         OnNewCallCallback callback) override;

 private:
  machina::Guest* guest_;
  fidl::Binding<machina::MessageListener> binding_;
  async::Loop loop_;
  component::ApplicationContext* ctx_;
};
