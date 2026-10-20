// SPDX-License-Identifier: BSD-3-Clause

#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <algorithm>
#include <stdint.h>
#include <string.h>
#include <zircon/device/vfs.h>
#include <fs/vfs.h>
#include <sys/stat.h>

#include "garnet/lib/littlefs/littlefs.h"
#include "garnet/lib/littlefs/vnode.h"

#include "lib/fxl/logging.h"

thread_local uint64_t g_tls_lfs_write_blks = 0;
bool enable_lfs_metrics = false;

namespace littlefs {

static zx_status_t lfs_error_to_zx_status(int lfs_err) {
  switch (lfs_err) {
    case LFS_ERR_OK:
      return ZX_OK;
    case LFS_ERR_IO:
      return ZX_ERR_IO;
    case LFS_ERR_CORRUPT:
      return ZX_ERR_IO_DATA_INTEGRITY;
    case LFS_ERR_NOENT:
      return ZX_ERR_NOT_FOUND;
    case LFS_ERR_EXIST:
      return ZX_ERR_ALREADY_EXISTS;
    case LFS_ERR_NOTDIR:
      return ZX_ERR_NOT_DIR;
    case LFS_ERR_ISDIR:
      return ZX_ERR_NOT_FILE;
    case LFS_ERR_NOTEMPTY:
      return ZX_ERR_BAD_STATE;
    case LFS_ERR_BADF:
      return ZX_ERR_BAD_HANDLE;
    case LFS_ERR_FBIG:
      return ZX_ERR_FILE_BIG;
    case LFS_ERR_INVAL:
      return ZX_ERR_INVALID_ARGS;
    case LFS_ERR_NOMEM:
      return ZX_ERR_NO_MEMORY;
    case LFS_ERR_NOATTR:
      return ZX_ERR_NOT_FOUND;
    case LFS_ERR_NAMETOOLONG:
      return ZX_ERR_BAD_PATH;
    case LFS_ERR_NOSPC:
      return ZX_ERR_NO_SPACE;
    default:
      return ZX_ERR_IO;
  }
}

zx_status_t VnodeLittleFs::LfsRemove() {
  FXL_VLOG(1) << "Delete path=" << path_.data();
  int ret = lfs_remove(lfs_, path_.data());
  if (ret < 0)
    return lfs_error_to_zx_status(ret);

  return ZX_OK;
}

zx_status_t VnodeLittleFs::LfsRename(fbl::String newpath) {
  FXL_VLOG(1) << "Rename oldpath=" << path_.data()
              << ", newpath=" << newpath.data();
  int ret = lfs_rename(lfs_, path_.data(), newpath.data());
  if (ret < 0)
    return lfs_error_to_zx_status(ret);

  path_ = fbl::move(newpath);
  return ZX_OK;
}

zx_status_t VnodeLittleFs::ValidateFlags(uint32_t flags) {
   FXL_VLOG(1) << "ValidateFlags=" << std::hex << flags;
  if ((flags & ZX_FS_FLAG_DIRECTORY) && !IsDirectory()) {
      return ZX_ERR_NOT_DIR;
  }

  if ((flags & ZX_FS_RIGHT_WRITABLE) && IsDirectory()) {
      return ZX_ERR_NOT_FILE;
  }
  return ZX_OK;
}

zx_status_t VnodeLittleFs::Getattr(vnattr_t* attr) {
  struct lfs_info info;
  int ret = lfs_stat(lfs_, path_.data(), &info);
  if (ret) {
      return lfs_error_to_zx_status(ret);;
  }

  memset(attr, 0, sizeof(vnattr_t));
  attr->mode = V_IRWXU | V_IRWXG | V_IRWXO;
  attr->nlink = 1;
  attr->size = info.size;

  switch (info.type) {
    case LFS_TYPE_DIR: attr->mode |= V_TYPE_DIR; break;
    case LFS_TYPE_REG: attr->mode |= V_TYPE_FILE; break;
  }
  return ZX_OK;
}

zx_status_t VnodeLittleFs::Ioctl(uint32_t op, const void* in_buf, size_t in_len,
                                void* out_buf, size_t out_len,
                                size_t* out_actual) {
  if (op != IOCTL_VFS_QUERY_FS) {
    return fs::Vnode::Ioctl(op, in_buf, in_len, out_buf, out_len, out_actual);
  }

  if (out_actual == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  *out_actual = 0;

  const char kFsName[] = "littlefs";
  const size_t name_len = strlen(kFsName);
  const size_t required_len = sizeof(vfs_query_info_t) + name_len;
  if (out_buf == nullptr || out_len < sizeof(vfs_query_info_t)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  lfs_ssize_t used_blocks = lfs_fs_size(lfs_);
  if (used_blocks < 0) {
    return lfs_error_to_zx_status(used_blocks);
  }

  memset(out_buf, 0, out_len);
  auto* info = reinterpret_cast<vfs_query_info_t*>(out_buf);
  const lfs_size_t block_size = lfs_->cfg ? lfs_->cfg->block_size : 0;
  info->total_bytes = static_cast<uint64_t>(lfs_->block_count) * block_size;
  info->used_bytes = static_cast<uint64_t>(used_blocks) * block_size;
  info->block_size = static_cast<uint32_t>(
      std::min<uint64_t>(block_size, UINT32_MAX));
  info->max_filename_size = static_cast<uint32_t>(
      std::min<uint64_t>(lfs_->name_max, UINT32_MAX));

  const size_t copy_len = std::min(name_len,
                                   out_len > sizeof(vfs_query_info_t)
                                       ? out_len - sizeof(vfs_query_info_t)
                                       : 0);
  if (copy_len > 0) {
    memcpy(info->name, kFsName, copy_len);
    info->fs_type = VFS_TYPE_LITTLEFS;
  }
  *out_actual = std::min(required_len, out_len);
  return out_len >= required_len ? ZX_OK : ZX_ERR_BUFFER_TOO_SMALL;
}

bool VnodeDir::IsEmpty() {
  fbl::AutoLock lock(&mutex_);
  return entries_.is_empty();
}

zx_status_t VnodeDir::Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) {
  FXL_VLOG(1) << "Dir Open, path=" << path_.data() << ", flags=0x" << std::hex
              << flags;
  fbl::AutoLock lock(&mutex_);

  if (entries_.is_empty())
    return PopulateLittleFsDirsLocked();

  return ZX_OK;
}

zx_status_t VnodeDir::Lookup(fbl::RefPtr<fs::Vnode>* out,
                             fbl::StringPiece name) {
  FXL_VLOG(1) << "Dir Lookup, path=" << path_.data()
              << ", name=" << name.data();
  fbl::AutoLock lock(&mutex_);

  for (const auto& entry : entries_) {
    if (entry.name().ToStringPiece() == name) {
      *out = entry.node();
      return ZX_OK;
    }
  }

  return ZX_ERR_NOT_FOUND;
}

zx_status_t VnodeDir::Close() {
  FXL_VLOG(1) << "Dir Close, path=" << path_.data();
  return ZX_OK;
}

zx_status_t VnodeDir::Readdir(fs::vdircookie_t* cookie,
                              void* data,
                              size_t len,
                              size_t* out_actual) {
  FXL_VLOG(1) << "Readdir, cookie=" << cookie->n;
  fs::DirentFiller df(data, len);
  zx_status_t r = 0;
  if (cookie->n < kDotId) {
    if ((r = df.Next(".", VTYPE_TO_DTYPE(V_TYPE_DIR))) != ZX_OK) {
      *out_actual = df.BytesFilled();
      return r;
    }
    cookie->n = kDotId;
  }

  fbl::AutoLock lock(&mutex_);

  for (const auto& entry : entries_) {
    if (cookie->n >= entry.id()) {
      continue;
    }

    vnattr_t attr;
    if ((r = entry.node()->Getattr(&attr)) != ZX_OK) {
      continue;
    }

    if ((r = df.Next(entry.name().ToStringPiece(),
                     VTYPE_TO_DTYPE(attr.mode))) != ZX_OK) {
      *out_actual = df.BytesFilled();
      return r;
    }
    cookie->n = entry.id();
  }

  *out_actual = df.BytesFilled();
  return ZX_OK;
}

zx_status_t VnodeDir::PopulateLittleFsDirsLocked() {
  struct lfs_info info;
  lfs_dir_t dir;
  int res = lfs_dir_open(lfs_, &dir, path_.c_str());
  if (res < 0) {
    return lfs_error_to_zx_status(res);
  }
  auto close_dir =
      fbl::MakeAutoCall([this, &dir]() { lfs_dir_close(lfs_, &dir); });

  while (true) {
    res = lfs_dir_read(lfs_, &dir, &info);
    if (res < 0) {
      break;
    }

    // end of directory
    if (res == 0) {
      break;
    }

    fbl::AllocChecker ac;
    fbl::RefPtr<VnodeLittleFs> vn;
    uint32_t mode;
    if (info.type == LFS_TYPE_DIR) {
      vn = fbl::AdoptRef(new (&ac) VnodeDir());
      mode = V_TYPE_DIR;
    } else if (info.type == LFS_TYPE_REG) {
      vn = fbl::AdoptRef(new (&ac) VnodeFile());
      mode = V_TYPE_FILE;
    }

    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }

    auto name = fbl::String(info.name);
    if (name == "." || name == "..") {
      continue;
    }

    auto path = fbl::String::Concat({path_, "/", name});
    vn->lfs_ = lfs_;
    vn->path_ = fbl::move(path);

    auto status = AddEntryLocked(fbl::String(info.name), vn);
    if (status == ZX_ERR_ALREADY_EXISTS) {
      continue;
    } else if (status != ZX_OK) {
      return status;
    }
  }
  return lfs_error_to_zx_status(res);
}

zx_status_t VnodeDir::CreateDirectory(fbl::RefPtr<fs::Vnode>* out,
                                      fbl::StringPiece name,
                                      uint32_t mode) {
  fbl::AllocChecker ac;
  auto vn = fbl::AdoptRef(new (&ac) VnodeDir());
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  auto path = fbl::String::Concat({path_, "/", name});
  int ret = lfs_mkdir(lfs_, path.c_str());
  if (ret < 0)
    return lfs_error_to_zx_status(ret);

  vn->lfs_ = lfs_;
  vn->path_ = std::move(path);
  AddEntry(fbl::String(name.data(), name.length()), vn);
  *out = fbl::move(vn);
  return ZX_OK;
}

zx_status_t VnodeDir::CreateFile(fbl::RefPtr<fs::Vnode>* out,
                                 fbl::StringPiece name,
                                 uint32_t mode) {
  fbl::AllocChecker ac;
  auto vn = fbl::AdoptRef(new (&ac) VnodeFile());
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  auto path = fbl::String::Concat({path_, "/", name});
  vn->lfs_ = lfs_;
  vn->path_ = std::move(path);
  AddEntry(fbl::String(name.data(), name.length()), vn);

  FXL_VLOG(1) << "CreateFile path=" << vn->path_.data() << ", mode=0x" << std::hex
                << mode;
  uint32_t flags = ZX_FS_FLAG_CREATE | ZX_FS_FLAG_EXCLUSIVE | ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE;

  fbl::RefPtr<fs::Vnode> redirect;
  zx_status_t status = vn->Open(flags, &redirect);
  if (status != ZX_OK) {
    return status;
  }

  *out = fbl::move(redirect);
  return ZX_OK;
}

zx_status_t VnodeDir::Create(fbl::RefPtr<fs::Vnode>* out,
                             fbl::StringPiece name,
                             uint32_t mode) {
  FXL_VLOG(1) << "Create name=" << name.data() << ", mode=0x" << std::hex
              << mode;
  fbl::RefPtr<fs::Vnode> vn;
  auto status = Lookup(&vn, name);
  if (status == ZX_OK) {
    return ZX_ERR_ALREADY_EXISTS;
  }

  if (!S_ISDIR(mode)) {
    return CreateFile(out, name, mode);
  } else {
    return CreateDirectory(out, name, mode);
  }
}

zx_status_t VnodeDir::Unlink(fbl::StringPiece name, bool must_be_dir) {
  FXL_VLOG(1) << "Unlink name=" << name.data()
              << ", must_be_dir=" << must_be_dir;
  fbl::RefPtr<Vnode> vn;
  auto status = Lookup(&vn, name);
  if (status)
    return status;

  auto lfs_entry = fbl::RefPtr<VnodeLittleFs>::Downcast(vn);
  if (must_be_dir && !lfs_entry->IsDirectory())
    return ZX_ERR_NOT_DIR;

  if (lfs_entry->IsDirectory() && !lfs_entry->IsEmpty())
    return ZX_ERR_NOT_EMPTY;

  status = lfs_entry->LfsRemove();
  if (status)
    return status;

  return RemoveEntry(name);
}

zx_status_t VnodeDir::Rename(fbl::RefPtr<fs::Vnode> newdir,
                             fbl::StringPiece oldname,
                             fbl::StringPiece newname,
                             bool src_must_be_dir,
                             bool dst_must_be_dir) {
  fbl::RefPtr<Vnode> vn;
  auto status = Lookup(&vn, oldname);
  if (status)
    return status;

  auto target_dir = fbl::RefPtr<VnodeDir>::Downcast(newdir);
  auto newpath = fbl::String::Concat({target_dir->path(), "/", newname});
  auto src_entry = fbl::RefPtr<VnodeLittleFs>::Downcast(vn);
  status = src_entry->LfsRename(newpath);
  if (status != ZX_OK)
    return status;

  // if the src entry is a directory, we need to invalidate all entries
  // under this direcotry, since all entries' path has changed.
  if (src_entry->IsDirectory()) {
    auto dir = fbl::RefPtr<VnodeDir>::Downcast(vn);
    dir->RemoveAllEntries();
  }

  status = RemoveEntry(oldname);
  FXL_CHECK(status == ZX_OK);

  status = target_dir->AddEntry(newname, vn);
  FXL_CHECK(status == ZX_OK || status == ZX_ERR_ALREADY_EXISTS);

  return ZX_OK;
}

zx_status_t VnodeDir::AddEntry(fbl::String name, fbl::RefPtr<fs::Vnode> vn) {
  fbl::AutoLock lock(&mutex_);
  return AddEntryLocked(fbl::move(name), vn);
}

zx_status_t VnodeDir::AddEntryLocked(fbl::String name,
                                     fbl::RefPtr<fs::Vnode> vn) {
  ZX_DEBUG_ASSERT(vn);

  if (!fs::vfs_valid_name(name.ToStringPiece())) {
    return ZX_ERR_INVALID_ARGS;
  }

  for (auto& entry : entries_) {
    if (entry.name() == name) {
      return ZX_ERR_ALREADY_EXISTS;
    }
  }

  auto entry = fbl::unique_ptr<Entry>(
      new Entry(next_node_id_++, fbl::move(name), fbl::move(vn)));
  entries_.push_back(fbl::move(entry));
  return ZX_OK;
}

zx_status_t VnodeDir::RemoveEntry(fbl::StringPiece name) {
  fbl::AutoLock lock(&mutex_);

  for (auto& entry : entries_) {
    if (entry.name().ToStringPiece() == name) {
      entries_.erase(entry);
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_FOUND;
}

void VnodeDir::RemoveAllEntries() {
  fbl::AutoLock lock(&mutex_);
  entries_.clear();
}

zx_status_t VnodeDir::Alloc(fbl::RefPtr<VnodeDir>* out,
                            lfs_t* lfs,
                            fbl::String path) {
  fbl::AllocChecker ac;
  auto dir = fbl::AdoptRef(new (&ac) VnodeDir());
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  dir->lfs_ = lfs;
  dir->path_ = fbl::move(path);
  *out = fbl::move(dir);
  return ZX_OK;
}

VnodeDir::Entry::Entry(uint64_t id,
                       fbl::String name,
                       fbl::RefPtr<fs::Vnode> node)
    : id_(id), name_(fbl::move(name)), node_(fbl::move(node)) {}

VnodeDir::Entry::~Entry() = default;

zx_status_t VnodeFile::Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) {
  FXL_VLOG(1) << "File Open, path=" << path_.data() << ", flags=0x" << std::hex
              << flags;

  if (flags & ZX_FS_FLAG_DIRECTORY)
    return ZX_ERR_NOT_DIR;

  *out_redirect = fbl::AdoptRef(new Content(fbl::WrapRefPtr(this), flags));
  Content *content_ptr = static_cast<Content*>(out_redirect->get());

  uint32_t lfs_flags = 0;
  if (flags & ZX_FS_RIGHT_READABLE)
    lfs_flags |= LFS_O_RDONLY;
  if (flags & ZX_FS_RIGHT_WRITABLE)
    lfs_flags |= LFS_O_WRONLY;
  if (flags & ZX_FS_FLAG_CREATE)
    lfs_flags |= LFS_O_CREAT;
  if (flags & ZX_FS_FLAG_EXCLUSIVE)
    lfs_flags |= LFS_O_EXCL;
  if (flags & ZX_FS_FLAG_TRUNCATE)
    lfs_flags |= LFS_O_TRUNC;
  if (flags & ZX_FS_FLAG_APPEND)
    lfs_flags |= LFS_O_APPEND;

  int ret = lfs_file_open(lfs_, content_ptr->lfs_file(), path_.data(), lfs_flags);
  zx_status_t status = lfs_error_to_zx_status(ret);

  if (status != ZX_OK) {
    out_redirect->reset();
  }
  return status;
}

VnodeFile::Content::Content(fbl::RefPtr<VnodeFile> vn_file, uint32_t flags)
    : vn_file_(fbl::move(vn_file)), flags_(flags) {
  memset(&file_, 0, sizeof(lfs_file_t));
}

VnodeFile::Content::~Content() = default;

zx_status_t VnodeFile::Content::Close() {
  FXL_VLOG(1) << "File Close, path=" << vn_file_->path_.data();
  g_tls_lfs_write_blks = 0;
  lfs_file_close(vn_file_->lfs_, &file_);
  session_write_blks_ += g_tls_lfs_write_blks;

  if (enable_lfs_metrics) {
    FXL_LOG(INFO) << "[LFS Stat] File closed: " << vn_file_->path_.data()
                  << " | Total UFS Write Blks (Data + Meta) = "
                  << session_write_blks_;
  }
  return ZX_OK;
}

zx_status_t VnodeFile::Content::Read(void* data,
                                     size_t len,
                                     size_t offset,
                                     size_t* out_actual) {
  ZX_DEBUG_ASSERT(fs::IsReadable(flags_));
  FXL_VLOG(1) << "File Read, path=" << vn_file_->path_.data();
  int ret = lfs_file_seek(vn_file_->lfs_, &file_, offset, LFS_SEEK_SET);
  if (ret < 0)
    return lfs_error_to_zx_status(ret);

  ret = lfs_file_read(vn_file_->lfs_, &file_, data, len);
  if (ret < 0)
    return lfs_error_to_zx_status(ret);

  *out_actual = ret;
  return ZX_OK;
}

zx_status_t VnodeFile::Content::Write(const void* data,
                                      size_t len,
                                      size_t offset,
                                      size_t* out_actual) {
  ZX_DEBUG_ASSERT(fs::IsWritable(flags_));
  FXL_VLOG(1) << "Vfs File Write, path=" << vn_file_->path_.data();
  g_tls_lfs_write_blks = 0;
  int ret = lfs_file_seek(vn_file_->lfs_, &file_, offset, LFS_SEEK_SET);
  if (ret < 0)
    return lfs_error_to_zx_status(ret);

  ret = lfs_file_write(vn_file_->lfs_, &file_, data, len);
  if (ret < 0)
    return lfs_error_to_zx_status(ret);

  *out_actual = ret;
  session_write_blks_ += g_tls_lfs_write_blks;
  return ZX_OK;
}

zx_status_t VnodeFile::Content::Append(const void* data,
                                       size_t len,
                                       size_t* out_end,
                                       size_t* out_actual) {
  ZX_DEBUG_ASSERT(fs::IsWritable(flags_));

  g_tls_lfs_write_blks = 0;
  int ret = lfs_file_seek(vn_file_->lfs_, &file_, 0, LFS_SEEK_END);
  if (ret < 0)
    return lfs_error_to_zx_status(ret);

  ret = lfs_file_write(vn_file_->lfs_, &file_, data, len);
  if (ret < 0)
    return lfs_error_to_zx_status(ret);
  *out_actual = ret;

  ret = lfs_file_tell(vn_file_->lfs_, &file_);
  if (ret < 0)
    return lfs_error_to_zx_status(ret);
  *out_end = ret;

  session_write_blks_ += g_tls_lfs_write_blks;
  return ZX_OK;
}

zx_status_t VnodeFile::Content::Getattr(vnattr_t* attr) {
  return vn_file_->Getattr(attr);
}

zx_status_t VnodeFile::Content::Truncate(size_t len) {
  ZX_DEBUG_ASSERT(fs::IsWritable(flags_));

  int ret = lfs_file_truncate(vn_file_->lfs_, &file_, len);
  if (ret < 0)
    return lfs_error_to_zx_status(ret);

  return ZX_OK;
}

void VnodeFile::Content::Sync(SyncCallback closure) {
    FXL_VLOG(1) << "File Sync, path=" << vn_file_->path_.data();
    g_tls_lfs_write_blks = 0;
    int ret = lfs_file_sync(vn_file_->lfs_, &file_);
    session_write_blks_ += g_tls_lfs_write_blks;
    zx_status_t status = lfs_error_to_zx_status(ret);
    closure(status);
}


}  // namespace littlefs