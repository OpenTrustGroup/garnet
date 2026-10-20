// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2023 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string.h>
#include <list>

#include <garnet/bin/guest/guest_config.h>
#include <garnet/bin/guest/proto/bootloader.pb.h>
#include <garnet/bin/guest/proto/vm_message.pb.h>
#include <garnet/lib/machina/fdt_utils.h>
#include <garnet/lib/machina/guest.h>

class Bootloader {
 public:
  struct LoadedImage {
    std::string name;
    uint64_t phys_base;
    size_t size;
  };
  static std::unique_ptr<Bootloader> BuildFromProto(std::string path,
                                                    machina::Guest& guest);

  Bootloader(nbl_vmm::BootloaderRecord& rec, machina::Guest& guest);
  virtual ~Bootloader() {}

  virtual zx_status_t Setup(GuestConfig& cfg,
                            uintptr_t* guest_ip,
                            std::vector<uint64_t>& extra_params) = 0;

  virtual void DumpStateHandler(const nbl_vmm::GuestDumpState& state,
                                uint32_t reason) = 0;

  LoadedImage *find_loaded_image(std::string name) {
    for (auto& image : loaded_images_) {
      if (name == image.name)
        return &image;
    }
    return nullptr;
  }

 protected:
  template <typename T>
  T* InspectMemory(uintptr_t gpaddr, size_t size = sizeof(T)) {
    auto off = gpaddr - phys_mem_.phys_base();
    return phys_mem_.as<T>(off, size);
  }

  machina::Guest& guest_;
  const machina::PhysMem& phys_mem_;
  std::vector<LoadedImage> loaded_images_;
};