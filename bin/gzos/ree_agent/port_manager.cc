#include "garnet/bin/gzos/ree_agent/port_manager.h"

namespace ree_agent {

void TipcPortManagerImpl::Publish(
    fidl::StringPtr port_path,
    fidl::InterfaceHandle<TipcPort> port,
    PublishCallback callback) {
  auto entry = fbl::make_unique<TipcPortTableEntry>(port_path.get(),
                                                    fbl::move(port));
  if (!entry) {
    callback(ZX_ERR_NO_MEMORY);
  }
  {
    fbl::AutoLock lock(&port_table_lock_);
    if (port_table_.insert_or_find(fbl::move(entry))) {
      callback(ZX_OK);
    } else {
      callback(ZX_ERR_ALREADY_EXISTS);
    }
  }
}

zx_status_t TipcPortManagerImpl::Find(fbl::String path,
                                      TipcPortTableEntry*& entry_out) {
  fbl::AutoLock lock(&port_table_lock_);
  auto iter = port_table_.find(path);

  if (!iter.IsValid()) {
    entry_out = nullptr;
    return ZX_ERR_NOT_FOUND;
  }

  entry_out = &(*iter);
  return ZX_OK;
}

}  // namespace ree_agent
