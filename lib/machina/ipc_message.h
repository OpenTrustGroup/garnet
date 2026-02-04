// Copyright 2024 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/cpp/machina.h>
#include <stdint.h>
#include <sys/types.h>
#include <memory>
#include <thread>

#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/guest_config.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/logging.h"

namespace machina {

class IpcMessage : public IpcMessageService {
 public:
  IpcMessage(component::ApplicationContext* application_context,
               GuestConfig& cfg);


  // |IpcMessageService|
  virtual void GetTraceBuf( GetTraceBufCallback callback) override;
  virtual void GetCamDmaBuf( GetCamDmaBufCallback callback) override;

  // |IpcMessageInit|
  void set_trace_mem(zx_vaddr_t addr, size_t size) {
    trace_mem_pa_ = addr;
    trace_mem_sz_ = size;
  }
  void SetCamDmabufMem(zx_vaddr_t addr, size_t size) {
    camdmabuf_mem_pa_ = addr;
    camdmabuf_mem_sz_ = size;
  }
 private:
  fidl::BindingSet<IpcMessageService> bindings_;

  zx_vaddr_t trace_mem_pa_ = 0;
  size_t trace_mem_sz_ = 0;

  zx_vaddr_t camdmabuf_mem_pa_ = 0;
  size_t camdmabuf_mem_sz_ = 0;

  async::Loop loop_;
};

}  // namespace machina
