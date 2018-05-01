// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/cpp/ree_agent.h>

#include <fbl/auto_lock.h>
#include <fbl/function.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/mutex.h>
#include <fbl/string.h>
#include <fbl/unique_ptr.h>
#include <zircon/misc/fnv1hash.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/synchronization/thread_annotations.h"

namespace ree_agent {

class TipcPortTableEntry
    : public fbl::SinglyLinkedListable<fbl::unique_ptr<TipcPortTableEntry>> {
 public:
  TipcPortTableEntry(const fbl::String path,
                     fidl::InterfaceHandle<TipcPort> port)
      : path_(path) {
    port_.Bind(fbl::move(port));
  }

  auto GetKey() const { return path_; }
  static size_t GetHash(fbl::String key) { return fnv1a64str(key.c_str()); }

  auto path() const { return path_; }

 private:
  fbl::String path_;
  TipcPortPtr port_;
};

class TipcPortManagerImpl : TipcPortManager {
 public:
  TipcPortManagerImpl() {}

  void Bind(fidl::InterfaceRequest<TipcPortManager> request) {
    if (request)
      bindings_.AddBinding(this, std::move(request));
  }

  zx_status_t Find(fbl::String path, TipcPortTableEntry*& entry_out);

  // TipcPortManager fidl implementation
  void Publish(fidl::StringPtr port_path,
               fidl::InterfaceHandle<TipcPort> port,
               PublishCallback callback) override;

 private:
  using HashTable =
      fbl::HashTable<fbl::String, fbl::unique_ptr<TipcPortTableEntry>>;

  fbl::Mutex port_table_lock_;
  HashTable port_table_ FXL_GUARDED_BY(port_table_lock_);
  fidl::BindingSet<TipcPortManager> bindings_;
};

}  // namespace ree_agent
