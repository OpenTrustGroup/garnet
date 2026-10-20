// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/string.h>
#include <fbl/unique_ptr.h>
#include <fs/vfs.h>
#include <fs/vnode.h>
#include <zircon/device/vfs.h>
#include <memory>
#include <string>

#include "garnet/lib/littlefs/bcache.h"
#include "third_party/littlefs/lfs.h"

extern thread_local uint64_t tls_lfs_write_blks;

namespace littlefs {

class VnodeLittleFs : public fs::Vnode {
 protected:
  virtual bool IsDirectory() { return false; }
  virtual bool IsEmpty() { return false; }
  zx_status_t LfsRemove();
  zx_status_t LfsRename(fbl::String newpath);
  const char* path() { return path_.data(); }

  // |Vnode| implementation:
  zx_status_t ValidateFlags(uint32_t flags) override;
  zx_status_t Getattr(vnattr_t* attr) final;
  zx_status_t Ioctl(uint32_t op, const void* in_buf, size_t in_len,
                    void* out_buf, size_t out_len, size_t* out_actual) final;

  friend class VnodeDir;
  lfs_t* lfs_;
  fbl::String path_;
};

class VnodeFile : public VnodeLittleFs {
 public:
  // |Vnode| implementation:
  zx_status_t Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) final;

 private:
  friend class VnodeDir;

  class Content final : public Vnode {
  public:
      Content(fbl::RefPtr<VnodeFile> vn_file, uint32_t flags);
      ~Content() override;

      // |Vnode| implementation:
      zx_status_t Close() final;
      zx_status_t Getattr(vnattr_t* attr) final;
      zx_status_t Read(void* data, size_t length, size_t offset, size_t* out_actual) final;
      zx_status_t Write(const void* data, size_t length, size_t offset, size_t* out_actual) final;
      zx_status_t Append(const void* data, size_t length, size_t* out_end, size_t* out_actual) final;
      zx_status_t Truncate(size_t length) final;
      void Sync(SyncCallback closure) final;
      lfs_file_t* lfs_file() { return &file_; }

      uint64_t session_write_blks_ = 0;
  private:
      //friend class VnodeFile;
      fbl::RefPtr<VnodeFile> const vn_file_;
      uint32_t const flags_;
      lfs_file_t file_;

  };
};

class VnodeDir : public VnodeLittleFs {
 public:
  virtual ~VnodeDir() = default;
  virtual bool IsDirectory() final { return true; }
  virtual bool IsEmpty() final;

  // fs::Vnode interface.
  zx_status_t Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) final;
  zx_status_t Lookup(fbl::RefPtr<fs::Vnode>* out, fbl::StringPiece name) final;
  zx_status_t Close() final;
  zx_status_t Readdir(fs::vdircookie_t* cookie,
                      void* dirents,
                      size_t len,
                      size_t* out_actual) final;
  zx_status_t Create(fbl::RefPtr<fs::Vnode>* out,
                     fbl::StringPiece name,
                     uint32_t mode) final;
  zx_status_t Rename(fbl::RefPtr<fs::Vnode> newdir,
                     fbl::StringPiece oldname,
                     fbl::StringPiece newname,
                     bool src_must_be_dir,
                     bool dst_must_be_dir) final;
  zx_status_t Unlink(fbl::StringPiece name, bool must_be_dir);

  zx_status_t AddEntryLocked(fbl::String name, fbl::RefPtr<fs::Vnode> vn)
      __TA_REQUIRES(mutex_);
  zx_status_t AddEntry(fbl::String name, fbl::RefPtr<fs::Vnode> vn);
  zx_status_t RemoveEntry(fbl::StringPiece name);
  void RemoveAllEntries();

  static zx_status_t Alloc(fbl::RefPtr<VnodeDir>* out,
                           lfs_t* lfs,
                           fbl::String path);
  zx_status_t PopulateLittleFsDirsLocked() __TA_REQUIRES(mutex_);
  zx_status_t CreateDirectory(fbl::RefPtr<fs::Vnode>* out,
                              fbl::StringPiece name,
                              uint32_t mode);
  zx_status_t CreateFile(fbl::RefPtr<fs::Vnode>* out,
                         fbl::StringPiece name,
                         uint32_t mode);

 private:
  static constexpr uint64_t kDotId = 1u;

  class Entry : public fbl::DoublyLinkedListable<fbl::unique_ptr<Entry>> {
   public:
    Entry(uint64_t id, fbl::String name, fbl::RefPtr<fs::Vnode> node);
    ~Entry();

    uint64_t id() const { return id_; }
    const fbl::String& name() const { return name_; }
    const fbl::RefPtr<fs::Vnode>& node() const { return node_; }

   private:
    uint64_t const id_;
    fbl::String name_;
    fbl::RefPtr<fs::Vnode> node_;
  };
  using EntryList = fbl::DoublyLinkedList<fbl::unique_ptr<Entry>>;

  fbl::Mutex mutex_;

  uint64_t next_node_id_ __TA_GUARDED(mutex_) = kDotId + 1;
  EntryList entries_ __TA_GUARDED(mutex_);
};
}  // namespace littlefs