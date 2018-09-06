// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include "garnet/bin/zxdb/client/finish_thread_controller.h"
#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/register.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/step_into_thread_controller.h"
#include "garnet/bin/zxdb/client/symbols/code_block.h"
#include "garnet/bin/zxdb/client/symbols/function.h"
#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/bin/zxdb/client/symbols/variable.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/client/until_thread_controller.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/format_frame.h"
#include "garnet/bin/zxdb/console/format_register.h"
#include "garnet/bin/zxdb/console/format_table.h"
#include "garnet/bin/zxdb/console/format_value.h"
#include "garnet/bin/zxdb/console/input_location_parser.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/console/verbs.h"
#include "garnet/bin/zxdb/expr/expr.h"
#include "garnet/bin/zxdb/expr/symbol_eval_context.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// If the system has at least one running process, returns true. If not,
// returns false and sets the err.
//
// When doing global things like System::Continue(), it will succeed if there
// are no running prograns (it will successfully continue all 0 processes).
// This is confusing to the user so this function is used to check first.
bool VerifySystemHasRunningProcess(System* system, Err* err) {
  for (const Target* target : system->GetTargets()) {
    if (target->GetProcess())
      return true;
  }
  *err = Err("No processes are running.");
  return false;
}

// backtrace -------------------------------------------------------------------

const char kBacktraceShortHelp[] = "backtrace / bt: Print a backtrace.";
const char kBacktraceHelp[] =
    R"(backtrace / bt

  Prints a backtrace of the selected thread. This is an alias for "frame -v".

  To see less information, use "frame" or just "f".

Examples

  t 2 bt
  thread 2 backtrace
)";
Err DoBacktrace(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread});
  if (err.has_error())
    return err;

  if (!cmd.thread())
    return Err("There is no thread to have frames.");

  OutputFrameList(cmd.thread(), true);
  return Err();
}

// continue --------------------------------------------------------------------

const char kContinueShortHelp[] =
    "continue / c: Continue a suspended thread or process.";
const char kContinueHelp[] =
    R"(continue / c

  When a thread is stopped at an exception or a breakpoint, "continue" will
  continue execution.

  See "pause" to stop a running thread or process.

  The behavior will depend upon the context specified.

  - By itself, "continue" will continue all threads of all processes that are
    currently stopped.

  - When a process is specified ("process 2 continue" for an explicit process
    or "process continue" for the current process), only the threads in that
    process will be continued. Other debugged processes currently stopped will
    remain so.

  - When a thread is specified ("thread 1 continue" for an explicit thread
    or "thread continue" for the current thread), only that thread will be
    continued. Other threads in that process and other processes currently
    stopped will remain so.

  TODO(brettw) it might be nice to have a --other flag that would continue
  all threads other than the specified one (which the user might want to step
  while everything else is going).

Examples

  c
  continue
      Continue all processes and threads.

  pr c
  process continue
  process 4 continue
      Contiue all threads of a process (the current process is implicit if
      no process index is specified).

  t c
  thread continue
  pr 2 t 4 c
  process 2 thread 4 continue
      Continue only one thread (the current process and thread are implicit
      if no index is specified).
)";
Err DoContinue(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread});
  if (err.has_error())
    return err;

  if (cmd.HasNoun(Noun::kThread)) {
    cmd.thread()->Continue();
  } else if (cmd.HasNoun(Noun::kProcess)) {
    Process* process = cmd.target()->GetProcess();
    if (!process)
      return Err("Process not running, can't continue.");
    process->Continue();
  } else {
    if (!VerifySystemHasRunningProcess(&context->session()->system(), &err))
      return err;
    context->session()->system().Continue();
  }

  return Err();
}

// finish ----------------------------------------------------------------------

const char kFinishShortHelp[] =
    "finish / fi: Finish execution of a stack frame.";
const char kFinishHelp[] =
    R"(finish / fi

  Alias: "fi"

  Resume thread execution until the selected stack frame returns. This means
  that the current function call will execute normally until it finished.

  See also "until".

Examples

  fi
  finish
      Exit the currently selected stack frame (see "frame").

  pr 1 t 4 fi
  process 1 thead 4 finish
      Applies "finish" to process 1, thread 4.

  f 2 fi
  frame 2 finish
      Exit frame 2, leaving program execution in what was frame 3. Try also
      "frame 3 until" which will do the same thing when the function is not
      recursive.
)";
Err DoFinish(ConsoleContext* context, const Command& cmd) {
  // This command allows "frame" which AssertStoppedThreadCommand doesn't,
  // so pass "false" to disabled noun checking and manually check ourselves.
  Err err = AssertStoppedThreadCommand(context, cmd, false, "finish");
  if (err.has_error())
    return err;
  err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kFrame});
  if (err.has_error())
    return err;

  auto controller = std::make_unique<FinishThreadController>(cmd.frame());
  cmd.thread()->ContinueWith(std::move(controller), [](const Err& err) {
    if (err.has_error())
      Console::get()->Output(err);
  });
  return Err();
}

// locals ----------------------------------------------------------------------

const char kLocalsShortHelp[] =
    "locals: Print local variables and function args.";
const char kLocalsHelp[] =
    R"(locals

  Prints all local variables and the current function's arguments. By default
  it will print the variables for the curretly selected stack frame.

  You can override the stack frame with the "frame" noun to get the locals
  for any specific stack frame of thread.

Examples

  locals
      Prints locals and args for the current stack frame.

  f 4 locals
  frame 4 locals
  thread 2 frame 3 locals
      Prints locals for a specific stack frame.
)";
Err DoLocals(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kFrame});
  if (err.has_error())
    return err;
  if (!cmd.frame())
    return Err("There isn't a current frame to read locals from.");

  const Location& location = cmd.frame()->GetLocation();
  if (!location.function())
    return Err("There is no symbol information for the frame.");
  const Function* function = location.function().Get()->AsFunction();
  if (!function)
    return Err("Symbols are corrupt.");

  // Find the innermost lexical block for the current IP.
  const CodeBlock* block = function->GetMostSpecificChild(
      location.symbol_context(), location.address());
  if (!block)
    return Err("There is no symbol information for the current IP.");

  // Walk upward in the hierarchy to collect local variables until hitting a
  // function. Using the map allows collecting only the innermost version of
  // a given name, and sorts them as we go.
  std::map<std::string, const Variable*> vars;
  while (block) {
    for (const auto& lazy_var : block->variables()) {
      const Variable* var = lazy_var.Get()->AsVariable();
      if (!var)
        continue;  // Symbols are corrupt.
      const std::string& name = var->GetAssignedName();
      if (vars.find(name) == vars.end())
        vars[name] = var;  // New one.
    }

    if (block == function)
      break;
    block = block->parent().Get()->AsCodeBlock();
  }

  // Add function parameters. Don't overwrite existing names in case of
  // duplicates to duplicate the shadowing rules of the language.
  for (const auto& param : function->parameters()) {
    const Variable* var = param.Get()->AsVariable();
    if (!var)
      continue;  // Symbols are corrupt.
    const std::string& name = var->GetAssignedName();
    if (vars.find(name) == vars.end())
      vars[name] = var;  // New one.
  }

  if (vars.empty()) {
    Console::get()->Output(
        OutputBuffer::WithContents("No local variables in scope."));
    return Err();
  }

  // TODO(brettw) hook this up with switches on the command.
  FormatValueOptions options;

  auto helper = fxl::MakeRefCounted<FormatValue>();
  for (const auto& pair : vars) {
    helper->AppendVariableWithName(location.symbol_context(),
                                   cmd.frame()->GetSymbolDataProvider(),
                                   pair.second, options);
    helper->Append(OutputBuffer::WithContents("\n"));
  }
  helper->Complete(
      [helper](OutputBuffer out) { Console::get()->Output(std::move(out)); });
  return Err();
}

// pause -----------------------------------------------------------------------

const char kPauseShortHelp[] = "pause / pa: Pause a thread or process.";
const char kPauseHelp[] =
    R"(pause / pa

  When a thread or process is running, "pause" will stop execution so state
  can be inspected or the thread single-stepped.

  See "continue" to resume a paused thread or process.

  The behavior will depend upon the context specified.

  - By itself, "pause" will pause all threads of all processes that are
    currently running.

  - When a process is specified ("process 2 pause" for an explicit process
    or "process pause" for the current process), only the threads in that
    process will be paused. Other debugged processes currently running will
    remain so.

  - When a thread is specified ("thread 1 pause" for an explicit thread
    or "thread pause" for the current thread), only that thread will be
    paused. Other threads in that process and other processes currently
    running will remain so.

  TODO(brettw) it might be nice to have a --other flag that would pause
  all threads other than the specified one.

Examples

  pa
  pause
      Pause all processes and threads.

  pr pa
  process pause
  process 4 pause
      Pause all threads of a process (the current process is implicit if
      no process index is specified).

  t pa
  thread pause
  pr 2 t 4 pa
  process 2 thread 4 pause
      Pause only one thread (the current process and thread are implicit
      if no index is specified).
)";
Err DoPause(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread});
  if (err.has_error())
    return err;

  if (cmd.HasNoun(Noun::kThread)) {
    cmd.thread()->Pause();
  } else if (cmd.HasNoun(Noun::kProcess)) {
    Process* process = cmd.target()->GetProcess();
    if (!process)
      return Err("Process not running, can't pause.");
    process->Pause();
  } else {
    if (!VerifySystemHasRunningProcess(&context->session()->system(), &err))
      return err;
    context->session()->system().Pause();
  }

  return Err();
}

// print -----------------------------------------------------------------------

const char kPrintShortHelp[] = "print / p: Print a variable or expression.";
const char kPrintHelp[] =
    R"(print <expression>

  Alias: p

  Evaluates a simple expression or variable name and prints the result.

  The expression is evaluated by default in the currently selected thread and
  stack frame. You can override this with "frame <x> print ...".

Expressions

  The expression evaluator understands the following C/C++ things:

    - Identifiers

    - Struct and class member access: . ->

    - Array access (for native arrays): [ <expression> ]

    - Create or dereference pointers: & *

    - Precedence: ( <expression> )

  Not supported: function calls, overloaded operators, casting.

Examples

  p foo
  print foo
      Print a variable

  p *foo->bar
  print &foo.bar[2]
      Deal with structs and arrays.

  f 2 p foo
  frame 2 print foo
  thread 1 frame 2 print foo
      Print a variable in the context of a specific stack frame.
)";
Err DoPrint(ConsoleContext* context, const Command& cmd) {
  Err err = AssertStoppedThreadCommand(context, cmd, false, "print");
  if (err.has_error())
    return err;
  err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kFrame});
  if (err.has_error())
    return err;
  if (!cmd.frame())
    return Err("There isn't a current frame for printing context.");

  // This takes one expression that may have spaces, so concatenate everything
  // the command parser has split apart back into one thing.
  //
  // If we run into limitations of this, we should add a "don't parse the args"
  // flag to the command record.
  std::string expr;
  for (const auto& cur : cmd.args()) {
    if (!expr.empty())
      expr.push_back(' ');
    expr += cur;
  }

  // TODO(brettw) parse options.
  FormatValueOptions options;

  auto data_provider = cmd.frame()->GetSymbolDataProvider();

  EvalExpression(expr, cmd.frame()->GetExprEvalContext(),
                 [options, data_provider](const Err& err, ExprValue value) {
                   if (err.has_error()) {
                     Console::get()->Output(err);
                   } else {
                     auto formatter = fxl::MakeRefCounted<FormatValue>();
                     formatter->AppendValue(data_provider, value, options);
                     // Bind the formatter to keep it in scope across this
                     // async call.
                     formatter->Complete([formatter](OutputBuffer out) {
                       Console::get()->Output(std::move(out));
                     });
                   }
                 });

  return Err();
}

// step ------------------------------------------------------------------------

const char kStepShortHelp[] =
    "step / s: Step one source line, going into subroutines.";
const char kStepHelp[] =
    R"(step

  Alias: "s"

  When a thread is stopped, "step" will execute one source line and stop the
  thread again. This will follow execution into subroutines. If the thread is
  running it will issue an error.

  By default, "step" will single-step the current thread. If a thread context
  is given, the specified thread will be stepped. You can't step a process.
  Other threads in the process will be unchanged so will remain running or
  stopped.

  See also "stepi".

Examples

  s
  step
      Step the current thread.

  t 2 s
  thread 2 step
      Steps thread 2 in the current process.
)";
Err DoStep(ConsoleContext* context, const Command& cmd) {
  Err err = AssertStoppedThreadCommand(context, cmd, true, "stepi");
  if (err.has_error())
    return err;

  auto controller = std::make_unique<StepIntoThreadController>();
  cmd.thread()->ContinueWith(std::move(controller), [](const Err& err) {
    if (err.has_error())
      Console::get()->Output(err);
  });
  return Err();
}

// stepi -----------------------------------------------------------------------

const char kStepiShortHelp[] =
    "stepi / si: Single-step a thread one machine instruction.";
const char kStepiHelp[] =
    R"(stepi / si

  When a thread is stopped, "stepi" will execute one machine instruction and
  stop the thread again. If the thread is running it will issue an error.

  By default, "stepi" will single-step the current thread. If a thread context
  is given, the specified thread will be single-stepped. You can't single-step
  a process.

Examples

  si
  stepi
      Step the current thread.

  t 2 si
  thread 2 stepi
      Steps thread 2 in the current process.

  pr 3 si
  process 3 stepi
      Steps the current thread in process 3 (regardless of which process is
      the current process).

  pr 3 t 2 si
  process 3 thread 2 stepi
      Steps thread 2 in process 3.
)";
Err DoStepi(ConsoleContext* context, const Command& cmd) {
  Err err = AssertStoppedThreadCommand(context, cmd, true, "stepi");
  if (err.has_error())
    return err;

  cmd.thread()->StepInstruction();
  return Err();
}

// regs ------------------------------------------------------------------------

using debug_ipc::RegisterCategory;

const char kRegsShortHelp[] =
    "regs / rg: Show the current registers for a thread.";
const char kRegsHelp[] =
    R"(regs [--category=<category>] [<regexp>]

  Alias: "rg"

  Shows the current registers for a thread. The thread must be stopped.
  By default the general purpose registers will be shown, but more can be
  configures through switches.

  NOTE: The values are displayed in the endianess of the target architecture.
        The interpretation of which bits are the MSB will vary across different
        endianess.

Arguments:

  --category=<category> | -c <category>
      Which categories if registers to show.
      The following options can be set:

      - general: Show general purpose registers.
      - fp: Show floating point registers.
      - vector: Show vector registries.
      - all: Show all the categories available.

      NOTE: not all categories exist within all architectures. For example,
            ARM64's fp category doesn't have any registers.

  <regexp>
      Case insensitive regular expression. Any register that matches will be
      shown. Uses POSIX Basic Regular Expression syntax. If not specified, it
      will match all registers.

Examples

  regs
  thread 4 regs --category=vector
  process 2 thread 1 regs -c all v*
)";

// Switches
constexpr int kRegsCategoriesSwitch = 1;
const std::vector<std::string> kRegsCategoriesValues = {"general", "fp",
                                                        "vector", "all"};

void OnRegsComplete(const Err& cmd_err, const RegisterSet& registers,
                    const std::string& reg_name,
                    const std::vector<RegisterCategory::Type>& cats_to_show) {
  Console* console = Console::get();
  if (cmd_err.has_error()) {
    console->Output(cmd_err);
    return;
  }

  OutputBuffer out;
  Err err = FormatRegisters(registers, reg_name, &out, cats_to_show);
  if (err.has_error()) {
    console->Output(err);
    return;
  }

  console->Output(out);
}

Err DoRegs(ConsoleContext* context, const Command& cmd) {
  Err err = AssertStoppedThreadCommand(context, cmd, true, "regs");
  if (err.has_error())
    return err;

  // When empty, we print all the registers.
  std::string reg_name;
  if (!cmd.args().empty()) {
    // We expect only one name.
    if (cmd.args().size() > 1u) {
      return Err("Only one register name expected.");
    }
    reg_name = cmd.args().front();
  }

  // Parse the switches

  // General purpose are the default.
  std::vector<RegisterCategory::Type> cats_to_show = {
      RegisterCategory::Type::kGeneral};
  if (cmd.HasSwitch(kRegsCategoriesSwitch)) {
    auto option = cmd.GetSwitchValue(kRegsCategoriesSwitch);
    if (option == "all") {
      cats_to_show = {debug_ipc::RegisterCategory::Type::kGeneral,
                      debug_ipc::RegisterCategory::Type::kFloatingPoint,
                      debug_ipc::RegisterCategory::Type::kVector,
                      debug_ipc::RegisterCategory::Type::kMisc};
    } else if (option == "general") {
      cats_to_show = {RegisterCategory::Type::kGeneral};
    } else if (option == "fp") {
      cats_to_show = {RegisterCategory::Type::kFloatingPoint};
    } else if (option == "vector") {
      cats_to_show = {RegisterCategory::Type::kVector};
    } else {
      return Err(fxl::StringPrintf("Unknown category: %s", option.c_str()));
    }
  }

  // We pass the given register name to the callback
  auto regs_cb = [ reg_name, cats = std::move(cats_to_show) ](
      const Err& err, const RegisterSet& registers) {
    OnRegsComplete(err, registers, reg_name, std::move(cats));
  };

  cmd.thread()->GetRegisters(regs_cb);
  return Err();
}

// until -----------------------------------------------------------------------

const char kUntilShortHelp[] =
    "until / u: Runs a thread until a location is reached.";
const char kUntilHelp[] =
    R"(until <location>

  Alias: "u"

  Continues execution of a thread or a process until a given location is
  reached. You could think of this command as setting an implicit one-shot
  breakpoint at the given location and continuing execution.

  Normally this operation will apply only to the current thread. To apply to
  all threads in a process, use "process until" (see the examples below).

  See also "finish".

Location arguments

  Current frame's address (no input)
    until

)" LOCATION_ARG_HELP("until")
        R"(
Examples

  u
  until
      Runs until the current frame's location is hit again. This can be useful
      if the current code is called in a loop to advance to the next iteration
      of the current code.

  f 1 u
  frame 1 until
      Runs until the given frame's location is hit. Since frame 1 is
      always the current function's calling frame, this command will normally
      stop when the current function returns. The exception is if the code
      in the calling function is called recursively from the current location,
      in which case the next invocation will stop ("until" does not match
      stack frames on break). See "finish" for a stack-aware version.

  u 24
  until 24
      Runs the current thread until line 24 of the current frame's file.

  until foo.cc:24
      Runs the current thread until the given file/line is reached.

  thread 2 until 24
  process 1 thread 2 until 24
      Runs the specified thread until line 24 is reached. When no filename is
      given, the specified thread's currently selected frame will be used.

  u MyClass::MyFunc
  until MyClass::MyFunc
      Runs the current thread until the given function is called.

  pr u MyClass::MyFunc
  process until MyClass::MyFunc
      Continues all threads of the current process, stopping the next time any
      of them call the function.
)";
Err DoUntil(ConsoleContext* context, const Command& cmd) {
  Err err;

  // Decode the location.
  //
  // The validation on this is a bit tricky. Most uses apply to the current
  // thread and take some implicit information from the current frame (which
  // requires the thread be stopped). But when doing a process-wide one, don't
  // require a currently stopped thread unless it's required to compute the
  // location.
  InputLocation location;
  if (cmd.args().empty()) {
    // No args means use the current location.
    if (!cmd.frame()) {
      return Err(ErrType::kInput,
                 "There isn't a current frame to take the location from.");
    }
    location = InputLocation(cmd.frame()->GetAddress());
  } else if (cmd.args().size() == 1) {
    // One arg = normal location (ParseInputLocation can handle null frames).
    Err err = ParseInputLocation(cmd.frame(), cmd.args()[0], &location);
    if (err.has_error())
      return err;
  } else {
    return Err(ErrType::kInput,
               "Expecting zero or one arg for the location.\n"
               "Formats: <function>, <file>:<line#>, <line#>, or *<address>");
  }

  auto callback = [](const Err& err) {
    if (err.has_error())
      Console::get()->Output(err);
  };

  // Dispatch the request.
  if (cmd.HasNoun(Noun::kProcess) && !cmd.HasNoun(Noun::kThread) &&
      !cmd.HasNoun(Noun::kFrame)) {
    // Process-wide ("process until ...").
    err = AssertRunningTarget(context, "until", cmd.target());
    if (err.has_error())
      return err;
    cmd.target()->GetProcess()->ContinueUntil(location, callback);
  } else {
    // Thread-specific.
    err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kFrame});
    if (err.has_error())
      return err;

    err = AssertStoppedThreadCommand(context, cmd, false, "until");
    if (err.has_error())
      return err;

    auto controller = std::make_unique<UntilThreadController>(location);
    cmd.thread()->ContinueWith(std::move(controller), [](const Err& err) {
      if (err.has_error())
        Console::get()->Output(err);
    });
  }
  return Err();
}

}  // namespace

void AppendThreadVerbs(std::map<Verb, VerbRecord>* verbs) {
  (*verbs)[Verb::kBacktrace] =
      VerbRecord(&DoBacktrace, {"backtrace", "bt"}, kBacktraceShortHelp,
                 kBacktraceHelp, CommandGroup::kQuery);
  (*verbs)[Verb::kContinue] =
      VerbRecord(&DoContinue, {"continue", "c"}, kContinueShortHelp,
                 kContinueHelp, CommandGroup::kStep, SourceAffinity::kSource);
  (*verbs)[Verb::kFinish] =
      VerbRecord(&DoFinish, {"finish", "fi"}, kFinishShortHelp, kFinishHelp,
                 CommandGroup::kStep);
  (*verbs)[Verb::kLocals] = VerbRecord(&DoLocals, {"locals"}, kLocalsShortHelp,
                                       kLocalsHelp, CommandGroup::kQuery);
  (*verbs)[Verb::kPause] =
      VerbRecord(&DoPause, {"pause", "pa"}, kPauseShortHelp, kPauseHelp,
                 CommandGroup::kProcess);
  (*verbs)[Verb::kPrint] = VerbRecord(&DoPrint, {"print", "p"}, kPrintShortHelp,
                                      kPrintHelp, CommandGroup::kQuery);

  // regs
  SwitchRecord regs_categories(kRegsCategoriesSwitch, true, "category", 'c');
  VerbRecord regs(&DoRegs, {"regs", "rg"}, kRegsShortHelp, kRegsHelp,
                  CommandGroup::kAssembly);
  regs.switches.push_back(regs_categories);
  (*verbs)[Verb::kRegs] = std::move(regs);

  (*verbs)[Verb::kStep] =
      VerbRecord(&DoStep, {"step", "s"}, kStepShortHelp, kStepHelp,
                 CommandGroup::kStep, SourceAffinity::kSource);
  (*verbs)[Verb::kStepi] =
      VerbRecord(&DoStepi, {"stepi", "si"}, kStepiShortHelp, kStepiHelp,
                 CommandGroup::kAssembly, SourceAffinity::kAssembly);
  (*verbs)[Verb::kUntil] = VerbRecord(&DoUntil, {"until", "u"}, kUntilShortHelp,
                                      kUntilHelp, CommandGroup::kStep);
}

}  // namespace zxdb
