// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/command_utils.h"

#include <inttypes.h>
#include <limits>
#include <stdio.h>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/console_context.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/public/lib/fxl/logging.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Appends the given string to the output, padding with spaces to the width
// as necessary.
void AppendPadded(const std::string& str,
                  int width,
                  Align align,
                  Syntax syntax,
                  bool is_last_col,
                  OutputBuffer* out) {
  std::string text;

  int pad = std::max(0, width - static_cast<int>(str.size()));
  if (align == Align::kRight)
    text.append(pad, ' ');

  text.append(str);

  // Padding on the right. Don't add for the last col.
  if (!is_last_col && align == Align::kLeft)
    text.append(pad, ' ');

  // Separator after columns for all but the last.
  if (!is_last_col)
    text.push_back(' ');

  out->Append(syntax, std::move(text));
}

}  // namespace

Err AssertRunningTarget(ConsoleContext* context,
                        const char* command_name,
                        Target* target) {
  Target::State state = target->GetState();
  if (state == Target::State::kRunning)
    return Err();
  return Err(
      ErrType::kInput,
      fxl::StringPrintf("%s requires a running process but process %d is %s.",
                        command_name, context->IdForTarget(target),
                        TargetStateToString(state).c_str()));
}

Err StringToUint64(const std::string& s, uint64_t* out) {
  *out = 0;
  if (s.empty())
    return Err(ErrType::kInput, "The empty string is not a number.");

  bool is_hex = s.size() > 2u && s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
  if (is_hex) {
    for (size_t i = 2; i < s.size(); i++) {
      if (!isxdigit(s[i]))
        return Err(ErrType::kInput, "Invalid hex number: + \"" + s + "\".");
    }
  } else {
    for (size_t i = 0; i < s.size(); i++) {
      if (!isdigit(s[i]))
        return Err(ErrType::kInput, "Invalid number: \"" + s + "\".");
    }
  }

  *out = strtoull(s.c_str(), nullptr, is_hex ? 16 : 10);
  return Err();
}

Err ReadUint64Arg(const Command& cmd,
                  size_t arg_index,
                  const char* param_desc,
                  uint64_t* out) {
  if (cmd.args().size() <= arg_index) {
    return Err(ErrType::kInput,
               fxl::StringPrintf("Not enough arguments when reading the %s.",
                                 param_desc));
  }
  Err result = StringToUint64(cmd.args()[arg_index], out);
  if (result.has_error()) {
    return Err(ErrType::kInput,
               fxl::StringPrintf("Invalid number \"%s\" when reading the %s.",
                                 cmd.args()[arg_index].c_str(), param_desc));
  }
  return Err();
}

Err ParseHostPort(const std::string& in_host,
                  const std::string& in_port,
                  std::string* out_host,
                  uint16_t* out_port) {
  if (in_host.empty())
    return Err(ErrType::kInput, "No host component specified.");
  if (in_port.empty())
    return Err(ErrType::kInput, "No port component specified.");

  // Trim brackets from the host name for IPv6 addresses.
  if (in_host.front() == '[' && in_host.back() == ']')
    *out_host = in_host.substr(1, in_host.size() - 2);
  else
    *out_host = in_host;

  // Re-use paranoid int64 parsing.
  uint64_t port64;
  Err err = StringToUint64(in_port, &port64);
  if (err.has_error())
    return err;
  if (port64 == 0 || port64 > std::numeric_limits<uint16_t>::max())
    return Err(ErrType::kInput, "Port value out of range.");
  *out_port = static_cast<uint16_t>(port64);

  return Err();
}

Err ParseHostPort(const std::string& input,
                  std::string* out_host,
                  uint16_t* out_port) {
  // Separate based on the last colon.
  size_t colon = input.rfind(':');
  if (colon == std::string::npos)
    return Err(ErrType::kInput, "Expected colon to separate host/port.");

  // If the host has a colon in it, it could be an IPv6 address. In this case,
  // require brackets around it to differentiate the case where people
  // supplied an IPv6 address and we just picked out the last component above.
  std::string host = input.substr(0, colon);
  if (host.empty())
    return Err(ErrType::kInput, "No host component specified.");
  if (host.find(':') != std::string::npos) {
    if (host.front() != '[' || host.back() != ']') {
      return Err(ErrType::kInput,
                 "For IPv6 addresses use either: \"[::1]:1234\"\n"
                 "or the two-parameter form: \"::1 1234");
    }
  }

  std::string port = input.substr(colon + 1);

  return ParseHostPort(host, port, out_host, out_port);
}

std::string TargetStateToString(Target::State state) {
  struct Mapping {
    Target::State state;
    const char* string;
  };
  static const Mapping mappings[] = {{Target::State::kNone, "Not running"},
                                     {Target::State::kStarting, "Starting"},
                                     {Target::State::kRunning, "Running"}};

  for (const Mapping& mapping : mappings) {
    if (mapping.state == state)
      return mapping.string;
  }
  FXL_NOTREACHED();
  return std::string();
}

std::string ThreadStateToString(debug_ipc::ThreadRecord::State state) {
  struct Mapping {
    debug_ipc::ThreadRecord::State state;
    const char* string;
  };
  static const Mapping mappings[] = {
      {debug_ipc::ThreadRecord::State::kNew, "New"},
      {debug_ipc::ThreadRecord::State::kRunning, "Running"},
      {debug_ipc::ThreadRecord::State::kSuspended, "Suspended"},
      {debug_ipc::ThreadRecord::State::kBlocked, "Blocked"},
      {debug_ipc::ThreadRecord::State::kDying, "Dying"},
      {debug_ipc::ThreadRecord::State::kDead, "Dead"}};

  for (const Mapping& mapping : mappings) {
    if (mapping.state == state)
      return mapping.string;
  }
  FXL_NOTREACHED();
  return std::string();
}

std::string BreakpointScopeToString(const ConsoleContext* context,
                                    const Breakpoint* breakpoint) {
  Target* target_scope = nullptr;
  Thread* thread_scope = nullptr;
  Breakpoint::Scope scope = breakpoint->GetScope(&target_scope, &thread_scope);
  switch (scope) {
    case Breakpoint::Scope::kSystem:
      return "Global";
    case Breakpoint::Scope::kTarget:
      return fxl::StringPrintf("pr %d", context->IdForTarget(target_scope));
    case Breakpoint::Scope::kThread:
      return fxl::StringPrintf(
          "pr %d t %d",
          context->IdForTarget(thread_scope->GetProcess()->GetTarget()),
          context->IdForThread(thread_scope));
    default:
      FXL_NOTREACHED();
      return std::string();
  }
}

std::string BreakpointStopToString(debug_ipc::Stop stop) {
  struct Mapping {
    debug_ipc::Stop stop;
    const char* string;
  };
  static const Mapping mappings[] = {{debug_ipc::Stop::kAll, "All"},
                                     {debug_ipc::Stop::kProcess, "Process"},
                                     {debug_ipc::Stop::kThread, "Thread"}};

  for (const Mapping& mapping : mappings) {
    if (mapping.stop == stop)
      return mapping.string;
  }
  FXL_NOTREACHED();
  return std::string();
}

std::string ExceptionTypeToString(debug_ipc::NotifyException::Type type) {
  struct Mapping {
    debug_ipc::NotifyException::Type type;
    const char* string;
  };
  static const Mapping mappings[] = {
      {debug_ipc::NotifyException::Type::kGeneral, "General"},
      {debug_ipc::NotifyException::Type::kHardware, "Hardware"},
      {debug_ipc::NotifyException::Type::kSoftware, "Software"}};
  for (const Mapping& mapping : mappings) {
    if (mapping.type == type)
      return mapping.string;
  }
  FXL_NOTREACHED();
  return std::string();
}

std::string DescribeTarget(const ConsoleContext* context,
                           const Target* target,
                           bool columns) {
  int id = context->IdForTarget(target);
  std::string state = TargetStateToString(target->GetState());

  // Koid string. This includes a trailing space when present so it can be
  // concat'd even when not present and things look nice.
  std::string koid_str;
  if (target->GetState() == Target::State::kRunning) {
    koid_str = fxl::StringPrintf(columns ? "%" PRIu64 " " : "koid=%" PRIu64 " ",
                                 target->GetProcess()->GetKoid());
  }

  const char* format_string;
  if (columns)
    format_string = "%3d %11s %8s";
  else
    format_string = "Process %d %s %s";
  std::string result =
      fxl::StringPrintf(format_string, id, state.c_str(), koid_str.c_str());

  // When running, use the object name if any.
  std::string name;
  if (target->GetState() == Target::State::kRunning)
    name = target->GetProcess()->GetName();

  // Otherwise fall back to the program name which is the first arg.
  if (name.empty()) {
    const std::vector<std::string>& args = target->GetArgs();
    if (args.empty())
      name += "<no name>";
    else
      name += args[0];
  }
  result += name;

  return result;
}

std::string DescribeThread(const ConsoleContext* context,
                           const Thread* thread,
                           bool columns) {
  std::string state = ThreadStateToString(thread->GetState());

  const char* format_string;
  if (columns)
    format_string = "%3d %9s %8" PRIu64 " %s";
  else
    format_string = "Thread %d %s koid=%" PRIu64 " %s";
  return fxl::StringPrintf(format_string, context->IdForThread(thread),
                           state.c_str(), thread->GetKoid(),
                           thread->GetName().c_str());
}

// Unlike the other describe command, this takes an ID because normally
// you know the index when calling into here, and it's inefficient to look up.
std::string DescribeFrame(const Frame* frame, int id) {
  // This will need symbols hooked up.
  return fxl::StringPrintf("Frame %d @ 0x%" PRIx64, id, frame->GetIP());
}

std::string DescribeBreakpoint(const ConsoleContext* context,
                               const Breakpoint* breakpoint,
                               bool columns) {
  const char* format_string;
  if (columns)
    format_string = "%3d %10s %8s %7s %5d %s";
  else
    format_string = "Breakpoint %d on %s, %s, stop=%s, hit=%d, @ %s";

  std::string scope = BreakpointScopeToString(context, breakpoint);
  std::string stop = BreakpointStopToString(breakpoint->GetStopMode());
  const char* enabled = breakpoint->IsEnabled() ? "Enabled" : "Disabled";

  std::string location =
      fxl::StringPrintf("0x%" PRIx64, breakpoint->GetAddressLocation());

  return fxl::StringPrintf(format_string, context->IdForBreakpoint(breakpoint),
                           scope.c_str(), enabled, stop.c_str(),
                           breakpoint->GetHitCount(), location.c_str());
}

void FormatColumns(const std::vector<ColSpec>& spec,
                   const std::vector<std::vector<std::string>>& rows,
                   OutputBuffer* out) {
  std::vector<int> max;  // Max width of each column.

  // Max widths of headings.
  bool has_head = false;
  for (const auto& col : spec) {
    max.push_back(col.head.size());
    has_head |= !col.head.empty();
  }

  // Max widths of contents.
  for (const auto& row : rows) {
    FXL_DCHECK(row.size() == max.size()) << "Column spec size doesn't match.";
    for (size_t i = 0; i < row.size(); i++)
      max[i] = std::max(max[i], static_cast<int>(row[i].size()));
  }

  // Apply the max widths.
  for (size_t i = 0; i < spec.size(); i++) {
    int max_width = spec[i].max_width;
    if (max_width > 0 && max_width < max[i])
      max[i] = max_width;
  }

  // Print heading.
  if (has_head) {
    for (size_t i = 0; i < max.size(); i++) {
      const ColSpec& col = spec[i];
      if (col.pad_left)
        out->Append(Syntax::kNormal, std::string(col.pad_left, ' '));
      AppendPadded(col.head, max[i], col.align, Syntax::kHeading,
                   i == max.size() - 1, out);
    }
    out->Append("\n");
  }

  // Print rows.
  for (const auto& row : rows) {
    std::string text;
    for (size_t i = 0; i < row.size(); i++) {
      const ColSpec& col = spec[i];
      if (col.pad_left)
        out->Append(Syntax::kNormal, std::string(col.pad_left, ' '));
      AppendPadded(row[i], max[i], col.align, col.syntax, i == max.size() - 1,
                   out);
    }
    out->Append("\n");
  }
}

}  // namespace zxdb
