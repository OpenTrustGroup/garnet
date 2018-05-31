// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/vmo/strings.h"
#include "lib/fsl/vmo/vector.h"

#include <zircon/syscalls.h>
#include <vector>

#include "lib/fxl/logging.h"

namespace fsl {

namespace {

template <typename Container>
bool VmoFromContainer(const Container& container, SizedVmo* sized_vmo_ptr) {
  FXL_CHECK(sized_vmo_ptr);

  uint64_t num_bytes = container.size();
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(num_bytes, 0u, &vmo);
  if (status < 0) {
    FXL_LOG(WARNING) << "zx::vmo::create failed: " << status;
    return false;
  }

  if (num_bytes > 0) {
    status = vmo.write(container.data(), 0, num_bytes);
    if (status < 0) {
      FXL_LOG(WARNING) << "zx::vmo::write failed: " << status;
      return false;
    }
  }

  *sized_vmo_ptr = SizedVmo(std::move(vmo), num_bytes);

  return true;
}

template <typename Container>
bool ContainerFromVmo(const zx::vmo& buffer, uint64_t num_bytes,
                      Container* container_ptr) {
  FXL_CHECK(container_ptr);

  container_ptr->resize(num_bytes);

  if (num_bytes == 0) {
    return true;
  }

  zx_status_t status = buffer.read(&(*container_ptr)[0], 0, num_bytes);
  if (status < 0) {
    FXL_LOG(WARNING) << "zx::vmo::read failed: " << status;
    return false;
  }

  return true;
}

}  // namespace

bool VmoFromString(const fxl::StringView& string, SizedVmo* sized_vmo) {
  return VmoFromContainer<fxl::StringView>(string, sized_vmo);
}

bool StringFromVmo(const SizedVmo& shared_buffer, std::string* string_ptr) {
  return ContainerFromVmo<std::string>(shared_buffer.vmo(),
                                       shared_buffer.size(), string_ptr);
}

bool StringFromVmo(const fuchsia::mem::Buffer& vmo_transport,
                   std::string* string_ptr) {
  if (!SizedVmo::IsSizeValid(vmo_transport.vmo, vmo_transport.size)) {
    return false;
  }
  return ContainerFromVmo<std::string>(vmo_transport.vmo, vmo_transport.size,
                                       string_ptr);
}

bool VmoFromVector(const std::vector<char>& vector, SizedVmo* sized_vmo) {
  return VmoFromContainer<std::vector<char>>(vector, sized_vmo);
}

bool VectorFromVmo(const SizedVmo& shared_buffer,
                   std::vector<char>* vector_ptr) {
  return ContainerFromVmo<std::vector<char>>(shared_buffer.vmo(),
                                             shared_buffer.size(), vector_ptr);
}

bool VectorFromVmo(const fuchsia::mem::Buffer& vmo_transport,
                   std::vector<char>* vector_ptr) {
  if (!SizedVmo::IsSizeValid(vmo_transport.vmo, vmo_transport.size)) {
    return false;
  }
  return ContainerFromVmo<std::vector<char>>(vmo_transport.vmo,
                                             vmo_transport.size, vector_ptr);
}

bool VmoFromVector(const std::vector<uint8_t>& vector, SizedVmo* sized_vmo) {
  return VmoFromContainer<std::vector<uint8_t>>(vector, sized_vmo);
}

bool VectorFromVmo(const SizedVmo& shared_buffer,
                   std::vector<uint8_t>* vector_ptr) {
  return ContainerFromVmo<std::vector<uint8_t>>(
      shared_buffer.vmo(), shared_buffer.size(), vector_ptr);
}

bool VectorFromVmo(const fuchsia::mem::Buffer& vmo_transport,
                   std::vector<uint8_t>* vector_ptr) {
  if (!SizedVmo::IsSizeValid(vmo_transport.vmo, vmo_transport.size)) {
    return false;
  }
  return ContainerFromVmo<std::vector<uint8_t>>(vmo_transport.vmo,
                                                vmo_transport.size, vector_ptr);
}

}  // namespace fsl
