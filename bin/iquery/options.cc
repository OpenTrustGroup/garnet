// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <set>
#include <string>

#include <lib/fxl/files/path.h>
#include <lib/fxl/strings/concatenate.h>
#include <lib/fxl/strings/substitute.h>

#include "garnet/bin/iquery/formatters/json.h"
#include "garnet/bin/iquery/formatters/text.h"
#include "garnet/bin/iquery/options.h"

namespace iquery {

namespace {

std::set<std::string> kKnownOptions = {
    "cat",        "absolute_paths", "find", "format",
    "full_paths", "help",           "ls",   "recursive",
};

// Validate whether the option is within the defined ones.
bool OptionExists(const std::string& option) {
  if (kKnownOptions.find(option) == kKnownOptions.end()) {
    FXL_LOG(ERROR) << "Unknown option \"" << option << "\"";
    return false;
  }
  return true;
}

Options::FormatterType GetFormatterType(const fxl::CommandLine& cmd_line) {
  std::string formatter = cmd_line.GetOptionValueWithDefault("format", "");
  if (formatter.empty() || formatter == "text") {
    return Options::FormatterType::TEXT;
  } else if (formatter == "json") {
    return Options::FormatterType::JSON;
  } else {
    FXL_LOG(ERROR) << "Cannot find formatter: " << formatter;
    return Options::FormatterType::UNSET;
  }
}

std::unique_ptr<Formatter> CreateFormatter(Options::FormatterType type) {
  switch (type) {
    case Options::FormatterType::TEXT:
      return std::make_unique<TextFormatter>();
    case Options::FormatterType::JSON:
      return std::make_unique<JsonFormatter>();
    case Options::FormatterType::UNSET:
      return nullptr;
  }
  return nullptr;
}

}  // namespace

Options::Options(const fxl::CommandLine& command_line) {
  // Validate options
  for (const fxl::CommandLine::Option& option : command_line.options()) {
    if (!OptionExists(option.name))
      return;
  }

  if (command_line.HasOption("cat") && !SetMode(command_line, Mode::CAT))
    return;
  else if (command_line.HasOption("find") && !SetMode(command_line, Mode::FIND))
    return;
  else if (command_line.HasOption("ls") && !SetMode(command_line, Mode::LS))
    return;
  else if (mode == Mode::UNSET)
    SetMode(command_line, Mode::CAT);

  formatter_type = GetFormatterType(command_line);
  formatter = CreateFormatter(formatter_type);
  if (!formatter)
    return;

  // Path formatting options.
  path_format = PathFormatting::NONE;
  if (command_line.HasOption("full_paths")) {
    path_format = PathFormatting::FULL;
  }
  if (command_line.HasOption("absolute_paths")) {
    path_format = PathFormatting::ABSOLUTE;
  }
  // Find has a special case, where none path formatting is not really useful.
  if (path_format == PathFormatting::NONE && mode == Mode::FIND)
    path_format = PathFormatting::FULL;

  recursive = command_line.HasOption("recursive");

  std::copy(command_line.positional_args().begin(),
            command_line.positional_args().end(), std::back_inserter(paths));

  // If everything went well, we mark this options as valid.
  valid_ = true;
}

void Options::Usage(const std::string& argv0) {
  std::cout << fxl::Substitute(
      R"txt(Usage: $0 (--cat|--find|--ls) [--recursive]
      [--format=<FORMAT>] [(--full_paths|--absolute_paths)]
      PATH [...PATH]

  Utility for querying exposed object directories.

  Mode options:
  --cat:  [DEFAULT] Print the data for the object(s) given by each PATH.
          Defining --recursive will also output the children for that object.
  --find: find all objects under PATH. For each sub-path, will stop at finding
          the first object. Defining --recursive will search the whole tree.
  --ls:   List the children of the object(s) given by PATH.

  --recursive: Whether iquery should continue inside an object. See each mode
             c to see how it modifies their behaviour.

  --format: What formatter to use for output. Available options are:
    - text: [DEFAULT] Simple text output meant for manual inspection.
    - json: JSON format meant for machine consumption.

  --full_paths:     Include the full path in object names.
  --absolute_paths: Include full absolute path in objectnames.
                    Overrides --full_paths.

  PATH: paths where to look for targets. The interpretation of those depends
        on the mode.
)txt",
      argv0);
}

bool Options::SetMode(const fxl::CommandLine& command_line, Mode m) {
  if (mode != Mode::UNSET) {
    Invalid(command_line.argv0(), "multiple modes specified");
    return false;
  }
  mode = m;
  return true;
}

void Options::Invalid(const std::string& argv0, std::string reason) {
  std::cerr << fxl::Substitute("Invalid command line args: $0\n", reason);
  Usage(argv0);
  valid_ = false;
}

}  // namespace iquery
