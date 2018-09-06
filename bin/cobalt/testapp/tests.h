// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_TESTAPP_TESTS_H
#define GARNET_BIN_COBALT_TESTAPP_TESTS_H

#include <memory>
#include <sstream>
#include <string>

#include "garnet/bin/cobalt/testapp/cobalt_testapp_encoder.h"
#include "garnet/bin/cobalt/testapp/cobalt_testapp_logger.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/svc/cpp/services.h"

namespace cobalt {
namespace testapp {

bool TestRareEventWithStrings(CobaltTestAppEncoder* encoder_);

bool TestRareEventWithIndices(CobaltTestAppEncoder* encoder_);

bool TestModuleUris(CobaltTestAppEncoder* encoder_);

bool TestNumStarsInSky(CobaltTestAppEncoder* encoder_);

bool TestSpaceshipVelocity(CobaltTestAppEncoder* encoder_);

bool TestAvgReadTime(CobaltTestAppEncoder* encoder_);

bool TestModulePairs(CobaltTestAppEncoder* encoder_);

bool TestRareEventWithStringsUsingBlockUntilEmpty(
    CobaltTestAppEncoder* encoder_);

bool TestRareEventWithIndicesUsingServiceFromEnvironment(
    CobaltTestAppEncoder* encoder_);

bool TestModInitializationTime(CobaltTestAppEncoder* encoder_);

bool TestAppStartupTime(CobaltTestAppEncoder* encoder_);

bool TestV1Backend(CobaltTestAppEncoder* encoder_);

bool TestLogEvent(CobaltTestAppLogger* logger_);

bool TestLogEventCount(CobaltTestAppLogger* logger_);

bool TestLogElapsedTime(CobaltTestAppLogger* logger_);

bool TestLogFrameRate(CobaltTestAppLogger* logger_);

bool TestLogMemoryUsage(CobaltTestAppLogger* logger_);

bool TestLogString(CobaltTestAppLogger* logger_);

bool TestLogTimer(CobaltTestAppLogger* logger_);

bool TestLogIntHistogram(CobaltTestAppLogger* logger_);

bool TestLogCustomEvent(CobaltTestAppLogger* logger_);

}  // namespace testapp
}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_TESTAPP_COBALT_TESTS_H
