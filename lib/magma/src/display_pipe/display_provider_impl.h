// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_SRC_DISPLAY_PIPE_DISPLAY_PROVIDER_IMPL_H_
#define GARNET_LIB_MAGMA_SRC_DISPLAY_PIPE_DISPLAY_PROVIDER_IMPL_H_

#include <unordered_map>

#include <fuchsia/cpp/display_pipe.h>

#include "lib/fidl/cpp/binding_set.h"
#include "garnet/lib/magma/src/display_pipe/image_pipe_impl.h"
#include "garnet/lib/magma/src/display_pipe/magma_connection.h"

namespace display_pipe {

class DisplayProviderImpl : public DisplayProvider {
 public:
  DisplayProviderImpl();
  ~DisplayProviderImpl() override;

  void GetInfo(GetInfoCallback callback) override;
  void BindPipe(::fidl::InterfaceRequest<images::ImagePipe> pipe) override;

  void AddBinding(fidl::InterfaceRequest<DisplayProvider> request);

 private:
  fidl::BindingSet<DisplayProvider> bindings_;
  std::shared_ptr<MagmaConnection> conn_;
  ImagePipeImpl image_pipe_;


  FXL_DISALLOW_COPY_AND_ASSIGN(DisplayProviderImpl);
};
}  // namespace display_pipe

#endif  // GARNET_LIB_MAGMA_SRC_DISPLAY_PIPE_DISPLAY_PROVIDER_IMPL_H_
