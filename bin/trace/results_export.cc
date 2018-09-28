// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/results_export.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "garnet/public/lib/fxl/files/file.h"

namespace tracing {

namespace {

const char kLabelKey[] = "label";
const char kTestSuiteKey[] = "test_suite";
const char kUnitKey[] = "unit";
const char kSplitFirstKey[] = "split_first";
const char kValuesKey[] = "values";

void EncodeResult(rapidjson::Writer<rapidjson::StringBuffer>* writer,
                  const measure::Result& result) {
  writer->StartObject();
  {
    writer->Key(kLabelKey);
    writer->String(result.label.c_str());

    if (!result.test_suite.empty()) {
      writer->Key(kTestSuiteKey);
      writer->String(result.test_suite.c_str());
    }

    writer->Key(kUnitKey);
    writer->String(result.unit.c_str());

    writer->Key(kSplitFirstKey);
    writer->Bool(result.split_first);

    writer->Key(kValuesKey);
    writer->StartArray();
    for (const auto& value : result.values) {
      writer->Double(value);
    }
    writer->EndArray();
  }
  writer->EndObject();
}

}  // namespace

bool ExportResults(const std::string& output_file_path,
                   const std::vector<measure::Result>& results) {
  rapidjson::StringBuffer string_buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(string_buffer);

  writer.StartArray();
  for (const auto& result : results) {
    EncodeResult(&writer, result);
  }
  writer.EndArray();

  std::string encoded = string_buffer.GetString();
  return files::WriteFile(output_file_path, encoded.data(), encoded.size());
}

}  // namespace tracing
