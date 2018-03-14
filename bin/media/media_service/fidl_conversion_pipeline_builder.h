// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "garnet/bin/media/framework/types/stream_type.h"
#include "lib/media/fidl/media_transport.fidl.h"

namespace media {

class MediaComponentFactory;

using ProducerGetter =
    std::function<void(f1dl::InterfaceRequest<MediaPacketProducer>)>;

using ConsumerGetter =
    std::function<void(f1dl::InterfaceRequest<MediaPacketConsumer>)>;

// Builds a pipeline of converters to convert packets of the specified type to
// a type in the goal set. If the call is successful, |callback| is called with
// a getter for the initial consumer in the pipeline and the stream type of the
// the packets that pipeline will produce. If the call isn't successful (no
// combination of conversions can be applied to produce a type in the goal set),
// |callback| is called with a null consumer getter and the stream type passed
// in the |type| argument. In the trivial case in which no converters are
// required, |callback| is called with the original getter and type.
void BuildFidlConversionPipeline(
    MediaComponentFactory* factory,
    const std::vector<std::unique_ptr<StreamTypeSet>>& goal_type_sets,
    const ProducerGetter& producer_getter,
    const ConsumerGetter& consumer_getter,
    std::unique_ptr<StreamType> type,
    const std::function<void(bool succeeded,
                             const ConsumerGetter&,
                             const ProducerGetter&,
                             std::unique_ptr<StreamType>)>& callback);

}  // namespace media
