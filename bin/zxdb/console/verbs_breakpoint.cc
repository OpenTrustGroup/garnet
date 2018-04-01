// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"

#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/console_context.h"
#include "garnet/bin/zxdb/console/output_buffer.h"

namespace zxdb {

namespace {

constexpr int kStopSwitch = 1;
constexpr int kEnableSwitch = 2;

// Backend for setting attributes on a breakpoint from both creation and
// editing. The given breakpoint is specified if this is an edit, or is null
// if this is a creation.
Err CreateOrEditBreakpoint(ConsoleContext* context,
                           const Command& cmd,
                           Breakpoint* breakpoint) {
  // Before doing any changes, validate all inputs.

  // Validate enabled flag (default to true for new breakpoints).
  bool enabled = breakpoint ? breakpoint->IsEnabled() : true;
  if (cmd.HasSwitch(kEnableSwitch)) {
    std::string enable_str = cmd.GetSwitchValue(kEnableSwitch);
    if (enable_str == "true") {
      enabled = true;
    } else if (enable_str == "false") {
      enabled = false;
    } else {
      return Err(
          "--enabled switch requires either \"true\" or \"false\" values.");
    }
  }

  // Stop flag (default to all for new breakpoints).
  debug_ipc::Stop stop =
      breakpoint ? breakpoint->GetStopMode() : debug_ipc::Stop::kAll;
  if (cmd.HasSwitch(kStopSwitch)) {
    std::string stop_str = cmd.GetSwitchValue(kStopSwitch);
    if (stop_str == "all") {
      stop = debug_ipc::Stop::kAll;
    } else if (stop_str == "process") {
      stop = debug_ipc::Stop::kProcess;
    } else if (stop_str == "thread") {
      stop = debug_ipc::Stop::kThread;
    } else {
      return Err(
          "--stop switch requires \"all\", \"process\", \"thread\", "
          "or \"none\".");
    }
  }

  // Location.
  uint64_t address = 0;
  if (cmd.args().empty()) {
    if (!breakpoint)
      return Err(ErrType::kInput, "New breakpoints must specify a location.");
  } else if (cmd.args().size() == 1u) {
    Err err = StringToUint64(cmd.args()[0], &address);
    if (err.has_error())
      return err;
  } else {
    return Err(ErrType::kInput, "Expecting only one arg for the location.");
  }

  // Everything validated, commit the changes.
  if (!breakpoint) {
    // New breakpoint.
    breakpoint = context->session()->system().CreateNewBreakpoint();
    context->SetActiveBreakpoint(breakpoint);
  }

  // Scope.
  Err err;
  if (cmd.HasNoun(Noun::kThread)) {
    err = breakpoint->SetScope(Breakpoint::Scope::kThread, cmd.target(),
                               cmd.thread());
  } else if (cmd.HasNoun(Noun::kProcess)) {
    err =
        breakpoint->SetScope(Breakpoint::Scope::kTarget, cmd.target(), nullptr);
  }
  if (err.has_error()) {
    FXL_NOTREACHED();  // Should have been validated enough above.
    return err;
  }
  // TODO(brettw) We don't have a "system" noun so there's no way to express
  // converting a process- or thread-specific breakpoint to a global one.
  // A system noun should be added and, if specified, this code should
  // convert to a global breakpoint.

  breakpoint->SetStopMode(stop);
  if (address)
    breakpoint->SetAddressLocation(address);

  breakpoint->SetEnabled(enabled);

  breakpoint->CommitChanges([](const Err& err) {
    if (err.has_error()) {
      OutputBuffer out;
      out.Append("Error setting breakpoint: ");
      out.OutputErr(err);
      Console::get()->Output(std::move(out));
    }
  });

  Console::get()->Output(DescribeBreakpoint(context, breakpoint, false));
  return err;
}

// break -----------------------------------------------------------------------

const char kBreakShortHelp[] = "break / br: Create a breakpoint.";
const char kBreakHelp[] =
    R"("break [ <address> ]

  Alias: "b"

  Creates or modifies a breakpoint. Not to be confused the the "breakpoint" /
  "bp" noun which lists breakpoints and modifies the breakpoint context. See
  "help bp" for more.

  The new breakpoint will become the active breakpoint so future breakpoint
  commands will apply to it by default.

Options

  --enable=[ true | false ]
  -e [ true | false ]

      Controls whether the breakpoint is enabled or disabled. A disabled
      breakpoint is never hit and hit counts are not incremented, but its
      settings are preserved. Defaults to enabled (true).

  --stop=[ all | process | thread ]
  -s [ all | process | thread ]

      Controls what execution is stopped when the breakpoint is hit. By
      default all threads of all debugged process will be stopped ("all") when
      a breakpoint is hit. But it's possible to only stop the threads of the
      current process ("process") or the thread that hit the breakpoint
      ("thread").

Scoping to processes and threads

  Explicit context can be provided to scope a breakpoint to a single process
  or a single thread. To do this, provide that process or thread as context
  before the break command:

    t 1 b 0x614a19837
    thread 1 break 0x614a19837
        Breaks on only this thread in the current process.

    pr 2 b 0x614a19837
    process 2 break 0x614a19837
        Breaks on all threads in the given process.

  When the thread of a thread-scoped breakpoint is destroyed, the breakpoint
  will be converted to a disabled process-scoped breakpoint. When the process
  context of a process-scoped breakpoint is destroyed, the breakpoint will be
  converted to a disabled global breakpoint.

Address breakpoints

  Currently only process-specific breakpoints on absolute addresses are
  supported. These are breakpoints at an explicit code address. Since
  addresses don't translate between processes, address breakpoints can only
  be set on running processes and will be automatically disabled when that
  process exits.

See also

  "help breakpoint": To list or select breakpoints.
  "help clear": To delete breakpoints.

Examples

  break 0x123c9df
  process 3 break 0x6123fd2937
  thread 1 break 0x67a82346
)";
Err DoBreak(ConsoleContext* context, const Command& cmd) {
  Err err =
      cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kBreakpoint});
  if (err.has_error())
    return err;
  return CreateOrEditBreakpoint(context, cmd, nullptr);
}

// clear -----------------------------------------------------------------------

const char kClearShortHelp[] = "clear / cl: Clear a breakpoint.";
const char kClearHelp[] =
    R"(clear

  Alias: "cl"

  By itself, "clear" will delete the current active breakpoint.

  Clear a named breakpoint by specifying the breakpoint context for the
  command. Unlike GDB, the context comes first, so instead of "clear 2" to
  clear breakpoint #2, use "breakpoint 2 clear" (or "bp 2 cl" for short).

See also

  "help break": To create breakpoints.
  "help breakpoint": To manage the current breakpoint context.

Examples

  breakpoint 2 clear
  bp 2 cl
  clear
  cl
)";
Err DoClear(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns({Noun::kBreakpoint});
  if (err.has_error())
    return err;

  // Expect no args. If an arg was specified, most likely they're trying to
  // use GDB syntax of "clear 2".
  if (cmd.args().size() > 0) {
    return Err(
        "\"clear\" takes no arguments. To specify an explicit "
        "breakpoint to clear,\nuse \"breakpoint <index> clear\" or "
        "\"bp <index> cl\" for short.");
  }

  if (!cmd.breakpoint()) {
    return Err(
        "There is no active breakpoint and no breakpoint was given.\n"
        "Use \"breakpoint <index> clear\" to specify one.\n");
  }

  std::string desc = DescribeBreakpoint(context, cmd.breakpoint(), false);
  context->session()->system().DeleteBreakpoint(cmd.breakpoint());
  Console::get()->Output("Deleted " + desc);
  return Err();
}

// edit ------------------------------------------------------------------------

const char kEditShortHelp[] = "edit / ed: Edit a breakpoint.";
const char kEditHelp[] =
    R"(edit

  Alias: "ed"

  Edits an existing breakpoint.  Edit requires an explicit context. The only
  context currently supported is "breakpoint". Specify an explicit breakpoint
  with the "breakpoint"/"bp" noun and its index:

    bp 4 ed ...
    breakpoint 4 edit ...

  Or use the active breakpoint by omitting the index:

    bp ed ...
    breakpoint edit ...

  The parameters accepted are any parameters accepted by the "break" command.
  Specified parameters will overwrite the existing settings. If a location is
  specified, the breakpoint will be moved, if a location is not specified, its
  location will be unchanged.

  The active breakpoint will not be changed.

See also

  "help break": To create breakpoints.
  "help breakpoint": To list and select the active breakpoint.

Examples

  bp 2 ed --enable=false
  breakpoint 2 edit --enable=false
      Disable breakpoint 2.

  bp ed --stop=thread
  breakpoint edit --stop=thread
      Make the active breakpoint stop only the thread that triggered it.

  pr 1 t 6 bp 7 b 0x614a19837
  process 1 thread 6 breakpoint 7 edit 0x614a19837
      Modifies breakpoint 7 to only break in process 1, thread 6 at the
      given address.
)";
Err DoEdit(ConsoleContext* context, const Command& cmd) {
  if (!cmd.HasNoun(Noun::kBreakpoint)) {
    // Edit requires an explicit "breakpoint" context so that in the future we
    // can apply edit to other nouns. I'm thinking any noun that can be created
    // can have its switches modified via an "edit" command that accepts the
    // same settings.
    return Err(ErrType::kInput,
               "\"edit\" requires an explicit breakpoint context.\n"
               "Either \"breakpoint edit\" for the active breakpoint, or "
               "\"breakpoint <index> edit\" for an\nexplicit one.");
  }

  Err err =
      cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kBreakpoint});
  if (err.has_error())
    return err;

  return CreateOrEditBreakpoint(context, cmd, cmd.breakpoint());
}

}  // namespace

void AppendBreakpointVerbs(std::map<Verb, VerbRecord>* verbs) {
  SwitchRecord enable_switch(kEnableSwitch, true, "enable", 'e');
  SwitchRecord stop_switch(kStopSwitch, true, "stop", 's');

  VerbRecord break_record(&DoBreak, {"break", "b"}, kBreakShortHelp,
                          kBreakHelp);
  break_record.switches.push_back(enable_switch);
  break_record.switches.push_back(stop_switch);
  (*verbs)[Verb::kBreak] = break_record;

  VerbRecord edit_record(&DoEdit, {"edit", "ed"}, kEditShortHelp, kEditHelp);
  edit_record.switches.push_back(enable_switch);
  edit_record.switches.push_back(stop_switch);
  (*verbs)[Verb::kEdit] = edit_record;

  (*verbs)[Verb::kClear] =
      VerbRecord(&DoClear, {"clear", "cl"}, kClearShortHelp, kClearHelp);
}

}  // namespace zxdb
