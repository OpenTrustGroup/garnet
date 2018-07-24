// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>

#include "lib/fsl/io/fd.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/concatenate.h"
#include "lib/fxl/strings/string_view.h"

#include "garnet/bin/gzos/sysmgr/manifest_parser.h"

namespace sysmgr {

constexpr char kTrustedAppDataPath[] = "/system/data";

void SafeCloseDir(DIR* dir) {
  if (dir) {
    closedir(dir);
  }
}

void ForEachManifest(ManifestCallback callback) {
  std::unique_ptr<DIR, decltype(&SafeCloseDir)> dir(
      opendir(kTrustedAppDataPath), SafeCloseDir);

  if (dir != NULL) {
    while (dirent* ta_entry = readdir(dir.get())) {
      // skip "." and ".."
      if (strcmp(".", ta_entry->d_name) == 0 ||
          strcmp("..", ta_entry->d_name) == 0) {
        continue;
      }

      std::string manifest_path = fxl::Concatenate(
          {kTrustedAppDataPath, "/", ta_entry->d_name, "/manifest.json"});

      if (files::IsFile(manifest_path)) {
        fxl::UniqueFD fd(open(manifest_path.c_str(), O_RDONLY));
        if (!fd.is_valid()) {
          FXL_LOG(WARNING) << "Failed to open " << manifest_path;
          continue;
        }

        std::string data;
        if (!files::ReadFileDescriptorToString(fd.get(), &data)) {
          FXL_LOG(WARNING) << "Failed to read " << manifest_path;
          continue;
        }

        rapidjson::Document document;
        document.Parse(data);
        if (document.HasParseError()) {
          FXL_LOG(WARNING) << "Failed to parse " << manifest_path;
          continue;
        }

        callback(std::string(ta_entry->d_name), manifest_path, document);
      }
    }
  } else {
    FXL_LOG(WARNING) << "Could not open directory " << kTrustedAppDataPath;
  }
}

void ParsePublicServices(
    const rapidjson::Document& document,
    std::function<void(const std::string& service)> callback) {
  auto it = document.FindMember("public_services");
  if (it == document.MemberEnd()) {
    return;
  }

  const auto& value = it->value;
  if (!value.IsArray()) {
    return;
  }

  for (rapidjson::SizeType i = 0; i < value.Size(); i++) {
    callback(value[i].GetString());
  }
}

void MountPackageData(fuchsia::sys::LaunchInfo& launch_info) {
  std::string package_data_path = fxl::Concatenate(
      {fxl::StringView(kTrustedAppDataPath), "/", launch_info.url.get()});
  fxl::UniqueFD fd(open(package_data_path.c_str(), O_RDONLY));
  if (fd.is_valid()) {
    auto flat_namespace = fuchsia::sys::FlatNamespace::New();
    flat_namespace->paths.push_back("pkg/data");
    flat_namespace->directories.push_back(
        fsl::CloneChannelFromFileDescriptor(fd.get()));

    launch_info.flat_namespace = std::move(flat_namespace);
  }
}

}  // namespace sysmgr
