// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_EVENT_REPORTER_H_
#define GARNET_LIB_UI_SCENIC_EVENT_REPORTER_H_

#include <fuchsia/cpp/ui.h>

namespace scenic {

// Interface for a class that submits events to the SessionListener.
class EventReporter {
 public:
  // Flushes enqueued session events to the session listener as a batch.
  virtual void SendEvents(::fidl::VectorPtr<ui::Event> buffered_events) = 0;
};

}  // namespace scenic

#endif  // GARNET_LIB_UI_SCENIC_EVENT_REPORTER_H_
