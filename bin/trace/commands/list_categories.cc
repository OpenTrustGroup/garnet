// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "garnet/bin/trace/commands/list_categories.h"

#include "lib/fsl/tasks/message_loop.h"

namespace tracing {

Command::Info ListCategories::Describe() {
  return Command::Info{[](app::ApplicationContext* context) {
                         return std::make_unique<ListCategories>(context);
                       },
                       "list-categories",
                       "list all known categories",
                       {}};
}

ListCategories::ListCategories(app::ApplicationContext* context)
    : CommandWithTraceController(context) {}

void ListCategories::Run(const fxl::CommandLine& command_line,
                         OnDoneCallback on_done) {
  if (!(command_line.options().empty() &&
        command_line.positional_args().empty())) {
    err() << "We encountered unknown options, please check your "
          << "command invocation" << std::endl;
  }

  trace_controller()->GetKnownCategories(
      [on_done = std::move(on_done)](
          f1dl::Array<KnownCategoryPtr> known_categories) {
        out() << "Known categories" << std::endl;
        for (const auto& it : known_categories) {
          out() << "  " << it->name.get() << ": " << it->description.get()
                << std::endl;
        }

        on_done(0);
      });
}

}  // namespace tracing
