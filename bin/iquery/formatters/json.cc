// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <third_party/rapidjson/rapidjson/prettywriter.h>

#include "garnet/bin/iquery/formatters/json.h"

namespace iquery {

namespace {

// Create the appropriate Json exporter according to the given options.
// NOTE(donoso): For some reason, rapidjson decided that while Writer is a class
//               PrettyWriter inherits from, it is *not* a virtual interface.
//               This means I cannot pass a Writer pointer around and get each
//               writer to do it's thing, which is what I expected.
//               Eventually this class will have to become a dispatcher that
//               passes the writer to a templatized version of FormatCat, Ls
//               and Find.
template <typename OutputStream>
std::unique_ptr<rapidjson::PrettyWriter<OutputStream>> GetJsonWriter(
    OutputStream& os, const Options&) {
  // NOTE(donosoc): When json formatter options are given, create the
  //                appropriate json writer and configure it here.
  return std::make_unique<rapidjson::PrettyWriter<OutputStream>>(os);
}

// FormatFind ------------------------------------------------------------------

std::string FormatFind(const Options& options,
                       const std::vector<ObjectNode>& results) {
  rapidjson::StringBuffer sb;
  auto writer = GetJsonWriter(sb, options);

  writer->StartArray();
  for (const auto& node : results) {
    // The path already considers the object name.
    writer->String(FormatPath(options.path_format, node.basepath, ""));
  }
  writer->EndArray();

  return sb.GetString();
}

// FormatLs --------------------------------------------------------------------

std::string FormatLs(const Options& options,
                     const std::vector<ObjectNode>& results) {
  rapidjson::StringBuffer sb;
  auto writer = GetJsonWriter(sb, options);

  writer->StartArray();
  for (const auto& node : results) {
    writer->String(
        FormatPath(options.path_format, node.basepath, node.object.name));
  }
  writer->EndArray();

  return sb.GetString();
}

// FormatCat -------------------------------------------------------------------

template <typename OutputStream>
void RecursiveFormatCat(rapidjson::PrettyWriter<OutputStream>* writer,
                        const Options& options, const ObjectNode& root) {
  writer->StartObject();

  // Properties.
  for (const auto& property : *root.object.properties) {
    writer->String(FormatString(property.key));
    writer->String(FormatString(property.value));
  }

  // Metrics.
  for (const auto& metric : *root.object.metrics) {
    writer->String(FormatString(metric.key));
    writer->String(FormatMetricValue(metric));
  }

  for (const auto& child : root.children) {
    writer->String(child.object.name);
    RecursiveFormatCat(writer, options, child);
  }

  writer->EndObject();
}

std::string FormatCat(const Options& options,
                      const std::vector<ObjectNode>& results) {
  rapidjson::StringBuffer sb;
  auto writer = GetJsonWriter(sb, options);

  writer->StartArray();
  for (const auto& node : results) {
    writer->StartObject();
    writer->String("path");
    writer->String(
        FormatPath(options.path_format, node.basepath, node.object.name));
    writer->String("contents");
    writer->StartObject();
    writer->String(node.object.name);
    RecursiveFormatCat(writer.get(), options, node);
    writer->EndObject();
    writer->EndObject();
  }
  writer->EndArray();

  return sb.GetString();
}

}  // namespace

std::string JsonFormatter::Format(const Options& options,
                                  const std::vector<ObjectNode>& results) {
  switch (options.mode) {
    case Options::Mode::CAT:
      return FormatCat(options, results);
    case Options::Mode::FIND:
      return FormatFind(options, results);
    case Options::Mode::LS:
      return FormatLs(options, results);
    case Options::Mode::UNSET: {
      FXL_LOG(ERROR) << "Unset Mode";
      return "";
    }
  }
}

}  // namespace iquery
