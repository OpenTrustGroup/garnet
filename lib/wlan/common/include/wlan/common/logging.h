// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <cstdio>

// TODO(hahnr): Allow change of logging prefix.
// TODO(tkilbourn): use standard logging infrastructure
namespace wlan {

constexpr uint64_t kLogLevelError = 1 << 0;
constexpr uint64_t kLogLevelWarning = 1 << 1;
constexpr uint64_t kLogLevelInfo = 1 << 2;
constexpr uint64_t kLogLevelDebug = 1 << 3;
constexpr uint64_t kLogLevelVerbose = 1 << 4;

constexpr uint64_t kLogErrors = kLogLevelError;
constexpr uint64_t kLogWarnings = kLogErrors | kLogLevelWarning;
constexpr uint64_t kLogInfos = kLogWarnings | kLogLevelInfo;
constexpr uint64_t kLogDebugs = kLogInfos | kLogLevelDebug;
constexpr uint64_t kLogVerboses = kLogDebugs | kLogLevelVerbose;

#define LOG_CATEGORY(name, value) constexpr uint64_t name = (1 << value)

LOG_CATEGORY(kLogDataFuncTrace, 16);
LOG_CATEGORY(kLogDataJoinTrace, 17);
LOG_CATEGORY(kLogDataHeaderTrace, 18);
LOG_CATEGORY(kLogDataPacketTrace, 19);
LOG_CATEGORY(kLogDataBeaconTrace, 20);
LOG_CATEGORY(kLogWlanFrameTrace, 21);
LOG_CATEGORY(kLogFrameHandlerTrace, 22);
LOG_CATEGORY(kLogFinspect, 23);  // Packet decoder log

#undef LOG_CATEGORY

// Set this to tune log output
constexpr uint64_t kLogLevel = kLogInfos;
constexpr bool kFinspectEnabled = kLogLevel & kLogFinspect;

#define finspect(args...) wlogf(wlan::kLogFinspect, "[finspect] ", args)

#define wlogf(level, level_prefix, args...)                                       \
    do {                                                                          \
        if (level & wlan::kLogLevel) { std::printf("wlan: " level_prefix args); } \
    } while (false)

// clang-format off
#define errorf(args...)   wlogf(wlan::kLogLevelError, "[E] ", args)
#define warnf(args...)    wlogf(wlan::kLogLevelWarning, "[W] ", args)
#define infof(args...)    wlogf(wlan::kLogLevelInfo, "[I] ", args)
#define debugf(args...)   wlogf(wlan::kLogLevelDebug, "[D] ", args)
#define verbosef(args...) wlogf(wlan::kLogLevelVerbose, "[V] ", args)

#define debugfn() wlogf(wlan::kLogDataFuncTrace, "[V:fn  ] ", "%s\n", __PRETTY_FUNCTION__)
#define debugjoin(args...) wlogf(wlan::kLogDataJoinTrace, "[V:join] ", args)
#define debughdr(args...)  wlogf(wlan::kLogDataHeaderTrace, "[V:hdr ] ", args)
#define debugbcn(args...)  wlogf(wlan::kLogDataBeaconTrace, "[V:bcn ] ", args)
#define debugbss(args...)  wlogf(wlan::kLogDataBeaconTrace, "[V:bss ] ", args)
#define debugfhandler(args...)  wlogf(wlan::kLogFrameHandlerTrace, "[V:fhdl] ", args)
// clang-format on

#define MAC_ADDR_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC_ADDR_ARGS(a) ((a)[0]), ((a)[1]), ((a)[2]), ((a)[3]), ((a)[4]), ((a)[5])

}  // namespace wlan
