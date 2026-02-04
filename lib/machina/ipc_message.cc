// Copyright 2024 GoldenRiver Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ipc_message.h"

namespace machina {


IpcMessage::IpcMessage(component::ApplicationContext* application_context,
                           GuestConfig& cfg){
  application_context->outgoing_services()->AddService<IpcMessageService>(
      [this](fidl::InterfaceRequest<IpcMessageService> request) {
        bindings_.AddBinding(this, std::move(request));
      });

  FXL_CHECK(loop_.StartThread() == ZX_OK);
}


void IpcMessage::GetTraceBuf(GetTraceBufCallback callback) {
  callback(trace_mem_pa_, trace_mem_sz_);
}

void IpcMessage::GetCamDmaBuf(GetCamDmaBufCallback callback) {
  callback(camdmabuf_mem_pa_, camdmabuf_mem_sz_);
}

}  // namespace machina
