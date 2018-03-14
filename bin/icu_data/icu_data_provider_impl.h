// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_ICU_DATA_ICU_DATA_PROVIDER_IMPL_H_
#define GARNET_BIN_ICU_DATA_ICU_DATA_PROVIDER_IMPL_H_

#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fxl/macros.h"
#include "lib/icu_data/fidl/icu_data.fidl.h"

namespace icu_data {

class ICUDataProviderImpl : public ICUDataProvider {
 public:
  ICUDataProviderImpl();
  ~ICUDataProviderImpl() override;

  // Return whether this function was able to successfully load the ICU data
  bool LoadData();

  void AddBinding(f1dl::InterfaceRequest<ICUDataProvider> request);

 private:
  // |ICUData| implementation:
  void ICUDataWithSha1(const f1dl::String& request,
                       const ICUDataWithSha1Callback& callback) override;

  f1dl::BindingSet<ICUDataProvider> bindings_;

  fsl::SizedVmo icu_data_vmo_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ICUDataProviderImpl);
};

}  // namespace icu_data

#endif  // GARNET_BIN_ICU_DATA_ICU_DATA_PROVIDER_IMPL_H_
