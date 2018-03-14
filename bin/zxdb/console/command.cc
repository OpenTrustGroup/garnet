// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/command.h"

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/console/verbs.h"
#include "garnet/public/lib/fxl/logging.h"

namespace zxdb {

namespace {

const char kFrameShortHelp[] =
    "frame";
const char kFrameHelp[] =
    R"(frame

  TODO write me.
)";

const char kThreadShortHelp[] =
    "thread";
const char kThreadHelp[] =
    R"(thread

  TODO write me.
)";

const char kProcessShortHelp[] =
    "process";
const char kProcessHelp[] =
    R"(process

  TODO write me.
)";

}  // namespace

const size_t Command::kNoIndex;

Command::Command() = default;
Command::~Command() = default;

bool Command::HasNoun(Noun noun) const {
  return nouns_.find(noun) != nouns_.end();
}

size_t Command::GetNounIndex(Noun noun) const {
  auto found = nouns_.find(noun);
  if (found == nouns_.end())
    return kNoIndex;
  return found->second;
}

void Command::SetNoun(Noun noun, size_t index) {
  FXL_DCHECK(nouns_.find(noun) == nouns_.end());
  nouns_[noun] = index;
}

bool Command::HasSwitch(int id) const {
  return switches_.find(id) != switches_.end();
}

std::string Command::GetSwitchValue(int id) const {
  auto found = switches_.find(id);
  if (found == switches_.end())
    return std::string();
  return found->second;
}

void Command::SetSwitch(int id, std::string str) {
  switches_[id] = std::move(str);
}

SwitchRecord::SwitchRecord() = default;
SwitchRecord::SwitchRecord(const SwitchRecord&) = default;
SwitchRecord::SwitchRecord(int i, bool has_value, const char* n, char c)
    : id(i), has_value(has_value), name(n), ch(c) {}
SwitchRecord::~SwitchRecord() = default;

NounRecord::NounRecord() = default;
NounRecord::NounRecord(std::initializer_list<std::string> aliases,
                       const char* short_help,
                       const char* help)
    : aliases(aliases), short_help(short_help), help(help) {}
NounRecord::~NounRecord() = default;

VerbRecord::VerbRecord() = default;
VerbRecord::VerbRecord(CommandExecutor exec,
                       std::initializer_list<std::string> aliases,
                       const char* short_help,
                       const char* help)
    : exec(exec), aliases(aliases), short_help(short_help), help(help) {}
VerbRecord::~VerbRecord() = default;

std::string NounToString(Noun n) {
  const auto& nouns = GetNouns();
  auto found = nouns.find(n);
  if (found == nouns.end())
    return std::string();
  return found->second.aliases[0];
}

std::string VerbToString(Verb v) {
  const auto& verbs = GetVerbs();
  auto found = verbs.find(v);
  if (found == verbs.end())
    return std::string();
  return found->second.aliases[0];
}

const std::map<Noun, NounRecord>& GetNouns() {
  static std::map<Noun, NounRecord> all_nouns;
  if (all_nouns.empty()) {
    all_nouns[Noun::kFrame] =
        NounRecord({"frame", "f"}, kFrameShortHelp, kFrameHelp);
    all_nouns[Noun::kThread] =
        NounRecord({"thread", "t"}, kThreadShortHelp, kThreadHelp);
    all_nouns[Noun::kProcess] =
        NounRecord({"process", "pr"}, kProcessShortHelp, kProcessHelp);

    // Everything but Noun::kNone (= 0) should be in the map.
    FXL_DCHECK(all_nouns.size() == static_cast<size_t>(Noun::kLast) - 1)
        << "You need to update the noun lookup table for additions to Nouns.";
  }
  return all_nouns;
}

const std::map<Verb, VerbRecord>& GetVerbs() {
  static std::map<Verb, VerbRecord> all_verbs;
  if (all_verbs.empty()) {
    AppendControlVerbs(&all_verbs);
    AppendMemoryVerbs(&all_verbs);
    AppendRunVerbs(&all_verbs);
    AppendSystemVerbs(&all_verbs);

    // Everything but Noun::kNone (= 0) should be in the map.
    FXL_DCHECK(all_verbs.size() == static_cast<size_t>(Verb::kLast) - 1)
        << "You need to update the verb lookup table for additions to Verbs.";
  }
  return all_verbs;
}

const std::map<std::string, Noun>& GetStringNounMap() {
  static std::map<std::string, Noun> map;
  if (map.empty()) {
    // Build up the reverse-mapping from alias to verb enum.
    for (const auto& noun_pair : GetNouns()) {
      for (const auto& alias : noun_pair.second.aliases)
        map[alias] = noun_pair.first;
    }
  }
  return map;
}

const std::map<std::string, Verb>& GetStringVerbMap() {
  static std::map<std::string, Verb> map;
  if (map.empty()) {
    // Build up the reverse-mapping from alias to verb enum.
    for (const auto& verb_pair : GetVerbs()) {
      for (const auto& alias : verb_pair.second.aliases)
        map[alias] = verb_pair.first;
    }
  }
  return map;
}

Err DispatchCommand(Session* session, const Command& cmd) {
  const auto& verbs = GetVerbs();
  const auto& found = verbs.find(cmd.verb());
  if (found == verbs.end()) {
    return Err(ErrType::kInput,
               "Invalid verb \"" + VerbToString(cmd.verb()) +
               "\".");
  }
  return found->second.exec(session, cmd);
}

}  // namespace zxdb
