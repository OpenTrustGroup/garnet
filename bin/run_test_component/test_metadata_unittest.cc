// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/run_test_component/test_metadata.h"

#include <string>
#include <utility>

#include <fuchsia/sys/cpp/fidl.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fxl/strings/substitute.h"

namespace run {
namespace {

using fuchsia::sys::LaunchInfo;

constexpr char kRequiredCmxElements[] = R"(
"program": {
  "binary": "path"
},
"sandbox": {
  "services": []
})";

class TestMetadataTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_NE("", tmp_dir_.path()) << "Cannot acccess /tmp";
  }

  void ExpectFailedParse(const std::string& json,
                         const std::string& expected_error) {
    std::string error;
    TestMetadata tm;
    EXPECT_FALSE(ParseFrom(&tm, json));
    EXPECT_TRUE(tm.HasError());
    EXPECT_THAT(tm.error_str(), ::testing::HasSubstr(expected_error));
  }

  bool ParseFrom(TestMetadata* tm, const std::string& json) {
    std::string json_path;
    if (!tmp_dir_.NewTempFileWithData(json, &json_path)) {
      return false;
    }
    const bool ret = tm->ParseFromFile(json_path);
    EXPECT_EQ(ret, !tm->HasError());
    return ret;
  }

  std::string CreateManifestJson(std::string additional_elements = "") {
    if (additional_elements == "") {
      return fxl::Substitute("{$0}", kRequiredCmxElements);
    } else {
      return fxl::Substitute("{$0, $1}", kRequiredCmxElements,
                             additional_elements);
    }
  }

 private:
  files::ScopedTempDir tmp_dir_;
};

TEST_F(TestMetadataTest, InvalidJson) {
  const std::string json = R"JSON({,,,})JSON";
  ExpectFailedParse(json, "Missing a name for object member.");
}

TEST_F(TestMetadataTest, NoFacet) {
  const std::string json = CreateManifestJson();
  TestMetadata tm;
  EXPECT_TRUE(ParseFrom(&tm, json));
  EXPECT_TRUE(tm.is_null());
}

TEST_F(TestMetadataTest, NoFuchsiaTestFacet) {
  const std::string json = CreateManifestJson(R"(
  "facets": {
  })");
  TestMetadata tm;
  EXPECT_TRUE(ParseFrom(&tm, json));
  EXPECT_TRUE(tm.is_null());
}

TEST_F(TestMetadataTest, NoServices) {
  const std::string json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": {
    }
  })");
  TestMetadata tm;
  EXPECT_TRUE(ParseFrom(&tm, json));
  EXPECT_FALSE(tm.is_null());
  ASSERT_FALSE(tm.HasServices());
}

TEST_F(TestMetadataTest, InvalidTestFacet) {
  const std::string json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": [
    ]
  })");
  ExpectFailedParse(json, "'fuchsia.test' in 'facets' should be an object.");
}

TEST_F(TestMetadataTest, InvalidServicesType) {
  const std::string json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": {
      "injected-services": []
    }
  })");
  ExpectFailedParse(json,
                    "'injected-services' in 'fuchsia.test' should be an "
                    "object.");
}

TEST_F(TestMetadataTest, InvalidSystemServicesType) {
  std::string json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": {
      "system-services": "string"
    }
  })");
  auto expected_error =
      "'system-services' in 'fuchsia.test' should be a string array.";
  ExpectFailedParse(json, expected_error);

  json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": {
      "system-services": {}
    }
  })");
  ExpectFailedParse(json, expected_error);

  json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": {
      "system-services": [ 2, 3 ]
    }
  })");
  ExpectFailedParse(json, expected_error);

  json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": {
      "system-services": [ "fuchsia.netstack.Netstack", "invalid_service" ]
    }
  })");
  ExpectFailedParse(json,
                    "'system-services' cannot contain 'invalid_service'.");
}

TEST_F(TestMetadataTest, InvalidServices) {
  std::string json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": {
      "injected-services": {
        1: "url"
      }
    }
  })");
  ExpectFailedParse(json, "Missing a name for object member.");

  json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": {
      "injected-services": {
        "1": 2
      }
    }
  })");
  ExpectFailedParse(json,
                    "'1' must be a string or a non-empty array of strings.");
  json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": {
      "injected-services": {
        "1": [2]
      }
    }
  })");

  ExpectFailedParse(json,
                    "'1' must be a string or a non-empty array of strings.");
}

TEST_F(TestMetadataTest, EmptyServices) {
  std::string json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": {
      "injected-services": {
      }
    }
  })");
  TestMetadata tm;
  EXPECT_TRUE(ParseFrom(&tm, json));
  EXPECT_FALSE(tm.HasError());
  EXPECT_FALSE(tm.HasServices());
}

std::pair<std::string, LaunchInfo> create_pair(const std::string& s1,
                                               LaunchInfo launch_info) {
  return std::make_pair(s1, std::move(launch_info));
}

TEST_F(TestMetadataTest, ValidServices) {
  std::string json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": {
      "injected-services": {
        "1": "url1",
        "2": ["url2", "--a=b", "c"],
        "3": "url3"
      }
    }
  })");

  TestMetadata tm;
  EXPECT_TRUE(ParseFrom(&tm, json));
  auto services = tm.TakeServices();
  ASSERT_EQ(3u, services.size());
  EXPECT_EQ(services[0], create_pair("1", LaunchInfo{.url = "url1"}));
  LaunchInfo launch_info{.url = "url2"};
  launch_info.arguments.push_back("--a=b");
  launch_info.arguments.push_back("c");
  EXPECT_EQ(services[1], create_pair("2", std::move(launch_info)));
  EXPECT_EQ(services[2], create_pair("3", LaunchInfo{.url = "url3"}));
  EXPECT_EQ(tm.system_services().size(), 0u);
}

TEST_F(TestMetadataTest, ValidSystemServices) {
  std::string json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": {
      "system-services": [
        "fuchsia.netstack.Netstack", "fuchsia.net.LegacySocketProvider",
        "fuchsia.net.Connectivity", "fuchsia.net.stack.Stack"
      ]
    }
  })");

  {
    TestMetadata tm;
    EXPECT_TRUE(ParseFrom(&tm, json));
    EXPECT_EQ(tm.system_services().size(), 4u);
    EXPECT_THAT(tm.system_services(),
                ::testing::ElementsAre("fuchsia.netstack.Netstack",
                                       "fuchsia.net.LegacySocketProvider",
                                       "fuchsia.net.Connectivity",
                                       "fuchsia.net.stack.Stack"));
  }

  json = CreateManifestJson(R"(
  "facets": {
    "fuchsia.test": {
      "system-services": [
        "fuchsia.netstack.Netstack", "fuchsia.net.LegacySocketProvider",
        "fuchsia.net.Connectivity"
      ]
    }
  })");
  {
    TestMetadata tm;
    EXPECT_TRUE(ParseFrom(&tm, json));
    EXPECT_EQ(tm.system_services().size(), 3u);
    EXPECT_THAT(tm.system_services(),
                ::testing::ElementsAre("fuchsia.netstack.Netstack",
                                       "fuchsia.net.LegacySocketProvider",
                                       "fuchsia.net.Connectivity"));
  }
}

}  // namespace
}  // namespace run
