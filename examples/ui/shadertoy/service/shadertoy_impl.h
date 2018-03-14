// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_SHADERTOY_SERVICE_SHADERTOY_IMPL_H_
#define GARNET_EXAMPLES_UI_SHADERTOY_SERVICE_SHADERTOY_IMPL_H_

#include "garnet/examples/ui/shadertoy/service/services/shadertoy.fidl.h"
#include "garnet/examples/ui/shadertoy/service/shadertoy_state.h"

namespace shadertoy {

// Thin wrapper that delegates Shadertoy API calls to a (subclass of)
// ShadertoyState.
class ShadertoyImpl : public mozart::example::Shadertoy {
 public:
  explicit ShadertoyImpl(fxl::RefPtr<ShadertoyState> state);
  ~ShadertoyImpl() override;

  ShadertoyState* state() const { return state_.get(); }

 private:
  // |Shadertoy|
  void SetPaused(bool paused) override;

  // |Shadertoy|
  void SetShaderCode(const ::f1dl::String& glsl,
                     const SetShaderCodeCallback& callback) override;

  // |Shadertoy|
  void SetResolution(uint32_t width, uint32_t height) override;

  // |Shadertoy|
  void SetMouse(scenic::vec4Ptr i_mouse) override;

  // |Shadertoy|
  void SetImage(uint32_t channel,
                ::f1dl::InterfaceRequest<scenic::ImagePipe> request) override;

  fxl::RefPtr<ShadertoyState> state_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ShadertoyImpl);
};

}  // namespace shadertoy

#endif  // GARNET_EXAMPLES_UI_SHADERTOY_SERVICE_SHADERTOY_IMPL_H_
