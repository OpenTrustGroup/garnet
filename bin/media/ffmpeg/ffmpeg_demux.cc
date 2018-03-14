// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <condition_variable>
#include <map>
#include <thread>

#include "garnet/bin/media/ffmpeg/av_codec_context.h"
#include "garnet/bin/media/ffmpeg/av_format_context.h"
#include "garnet/bin/media/ffmpeg/av_io_context.h"
#include "garnet/bin/media/ffmpeg/av_packet.h"
#include "garnet/bin/media/ffmpeg/ffmpeg_demux.h"
#include "garnet/bin/media/util/incident.h"
#include "garnet/bin/media/util/safe_clone.h"
#include "garnet/bin/media/util/thread_aware_shared_ptr.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/media/fidl/problem.fidl.h"
#include "lib/media/timeline/timeline_rate.h"

namespace media {

class FfmpegDemuxImpl : public FfmpegDemux {
 public:
  FfmpegDemuxImpl(std::shared_ptr<Reader> reader);

  ~FfmpegDemuxImpl() override;

  // Demux implementation.
  void SetStatusCallback(const StatusCallback& callback) override;

  void WhenInitialized(std::function<void(Result)> callback) override;

  const std::vector<DemuxStream*>& streams() const override;

  void Seek(int64_t position, const SeekCallback& callback) override;

  // ActiveMultistreamSource implementation.
  size_t stream_count() const override;

  void RequestPacket() override;

 private:
  static constexpr int64_t kNotSeeking = std::numeric_limits<int64_t>::max();

  class FfmpegDemuxStream : public DemuxStream {
   public:
    FfmpegDemuxStream(const AVFormatContext& format_context, size_t index);

    ~FfmpegDemuxStream() override;

    // Demux::DemuxStream implementation.
    size_t index() const override;

    std::unique_ptr<StreamType> stream_type() const override;

    TimelineRate pts_rate() const override;

   private:
    AVStream* stream_;
    size_t index_;
    std::unique_ptr<StreamType> stream_type_;
    TimelineRate pts_rate_;
  };

  // Specialized packet implementation.
  class DemuxPacket : public Packet {
   public:
    static PacketPtr Create(ffmpeg::AvPacketPtr av_packet,
                            TimelineRate pts_rate) {
      return std::make_shared<DemuxPacket>(std::move(av_packet), pts_rate);
    }

    AVPacket& av_packet() { return *av_packet_; }

    DemuxPacket(ffmpeg::AvPacketPtr av_packet, TimelineRate pts_rate)
        : Packet(
              (av_packet->pts == AV_NOPTS_VALUE) ? kUnknownPts : av_packet->pts,
              pts_rate,
              av_packet->flags & AV_PKT_FLAG_KEY,
              false,
              static_cast<size_t>(av_packet->size),
              av_packet->size == 0 ? nullptr : av_packet->data),
          av_packet_(std::move(av_packet)) {
      FXL_DCHECK(av_packet_->size >= 0);
    }

   private:
    ffmpeg::AvPacketPtr av_packet_;
  };

  // Runs in the ffmpeg thread doing the real work.
  void Worker();
  bool Wait(bool* packet_requested,
            int64_t* seek_position,
            SeekCallback* seek_callback);

  // Produces a packet. Called from the ffmpeg thread only.
  PacketPtr PullPacket(size_t* stream_index_out);

  // Produces an end-of-stream packet for next_stream_to_end_. Called from the
  // ffmpeg thread only.
  PacketPtr PullEndOfStreamPacket(size_t* stream_index_out);

  // Copies metadata from the specified source into map.
  void CopyMetadata(AVDictionary* source,
                    std::map<std::string, std::string>& map);

  // Calls the status callback, if there is one.
  void SendStatus();

  // Sets the problem values and sends status.
  void ReportProblem(const std::string& type, const std::string& details);

  std::mutex mutex_;
  std::condition_variable condition_variable_ FXL_GUARDED_BY(mutex_);
  std::thread ffmpeg_thread_;

  int64_t seek_position_ FXL_GUARDED_BY(mutex_) = kNotSeeking;
  SeekCallback seek_callback_ FXL_GUARDED_BY(mutex_);
  bool packet_requested_ FXL_GUARDED_BY(mutex_) = false;
  bool terminating_ FXL_GUARDED_BY(mutex_) = false;
  std::unique_ptr<Metadata> metadata_ FXL_GUARDED_BY(mutex_);
  std::string problem_type_ FXL_GUARDED_BY(mutex_);
  std::string problem_details_ FXL_GUARDED_BY(mutex_);

  // These should be stable after init until the desctructor terminates.
  std::shared_ptr<Reader> reader_;
  std::vector<DemuxStream*> streams_;
  Incident init_complete_;
  Result result_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  // After Init, only the ffmpeg thread accesses these.
  AvFormatContextPtr format_context_;
  AvIoContextPtr io_context_;
  int64_t next_pts_;
  int next_stream_to_end_ = -1;  // -1: don't end, streams_.size(): stop.

  StatusCallback status_callback_;
};

// static
std::shared_ptr<Demux> FfmpegDemux::Create(std::shared_ptr<Reader> reader) {
  return ThreadAwareSharedPtr<Demux>(
      new FfmpegDemuxImpl(reader),
      fsl::MessageLoop::GetCurrent()->task_runner());
}

FfmpegDemuxImpl::FfmpegDemuxImpl(std::shared_ptr<Reader> reader)
    : reader_(reader) {
  task_runner_ = fsl::MessageLoop::GetCurrent()->task_runner();
  FXL_DCHECK(task_runner_);
  ffmpeg_thread_ = std::thread([this]() { Worker(); });
}

FfmpegDemuxImpl::~FfmpegDemuxImpl() {
  {
    std::lock_guard<std::mutex> locker(mutex_);
    terminating_ = true;
    condition_variable_.notify_all();
  }

  if (ffmpeg_thread_.joinable()) {
    ffmpeg_thread_.join();
  }
}

void FfmpegDemuxImpl::SetStatusCallback(const StatusCallback& callback) {
  status_callback_ = callback;
}

void FfmpegDemuxImpl::WhenInitialized(std::function<void(Result)> callback) {
  init_complete_.When([this, callback]() { callback(result_); });
}

const std::vector<Demux::DemuxStream*>& FfmpegDemuxImpl::streams() const {
  return streams_;
}

void FfmpegDemuxImpl::Seek(int64_t position, const SeekCallback& callback) {
  std::lock_guard<std::mutex> locker(mutex_);
  seek_position_ = position;
  seek_callback_ = callback;
  condition_variable_.notify_all();
}

size_t FfmpegDemuxImpl::stream_count() const {
  return streams_.size();
}

void FfmpegDemuxImpl::RequestPacket() {
  std::lock_guard<std::mutex> locker(mutex_);
  packet_requested_ = true;
  condition_variable_.notify_all();
}

void FfmpegDemuxImpl::Worker() {
  static constexpr uint64_t kNanosecondsPerMicrosecond = 1000;

  result_ = AvIoContext::Create(reader_, &io_context_);
  if (result_ != Result::kOk) {
    FXL_LOG(ERROR) << "AvIoContext::Create failed, result "
                   << static_cast<int>(result_);
    ReportProblem(result_ == Result::kNotFound ? Problem::kProblemAssetNotFound
                                               : Problem::kProblemInternal,
                  "");
    init_complete_.Occur();
    return;
  }

  FXL_DCHECK(io_context_);

  format_context_ = AvFormatContext::OpenInput(io_context_);
  if (!format_context_) {
    FXL_LOG(ERROR) << "AvFormatContext::OpenInput failed";
    result_ = Result::kUnsupportedOperation;
    ReportProblem(Problem::kProblemContainerNotSupported, "");
    init_complete_.Occur();
    return;
  }

  int r = avformat_find_stream_info(format_context_.get(), nullptr);
  if (r < 0) {
    FXL_LOG(ERROR) << "avformat_find_stream_info failed, result " << r;
    result_ = Result::kInternalError;
    ReportProblem(Problem::kProblemInternal,
                  "avformat_find_stream_info failed");
    init_complete_.Occur();
    return;
  }

  std::map<std::string, std::string> metadata_map;

  CopyMetadata(format_context_->metadata, metadata_map);
  for (uint32_t i = 0; i < format_context_->nb_streams; i++) {
    streams_.push_back(new FfmpegDemuxStream(*format_context_, i));
    CopyMetadata(format_context_->streams[i]->metadata, metadata_map);
  }

  {
    std::lock_guard<std::mutex> locker(mutex_);
    metadata_ =
        Metadata::Create(format_context_->duration * kNanosecondsPerMicrosecond,
                         metadata_map["TITLE"], metadata_map["ARTIST"],
                         metadata_map["ALBUM"], metadata_map["PUBLISHER"],
                         metadata_map["GENRE"], metadata_map["COMPOSER"]);
  }

  result_ = Result::kOk;
  init_complete_.Occur();

  task_runner_->PostTask([this]() { SendStatus(); });

  while (true) {
    bool packet_requested;
    int64_t seek_position;
    SeekCallback seek_callback;

    if (!Wait(&packet_requested, &seek_position, &seek_callback))
      return;

    if (seek_position != kNotSeeking) {
      // AVSEEK_FLAG_BACKWARD tells the demux to search backward from the
      // specified seek position to the first i-frame it finds. We'll start
      // producing packets from there so the decoder has the context it needs.
      // The renderers throw away the packets that occur between the i-frame
      // and the seek position.
      int r = av_seek_frame(format_context_.get(), -1, seek_position / 1000,
                            AVSEEK_FLAG_BACKWARD);
      if (r < 0) {
        FXL_LOG(WARNING) << "av_seek_frame failed, result " << r;
      }
      next_stream_to_end_ = -1;
      seek_callback();
    }

    if (packet_requested) {
      size_t stream_index;
      PacketPtr packet = PullPacket(&stream_index);
      FXL_DCHECK(packet);

      // TODO(dalesat): Resolve the race that makes this necessary.
      task_runner_->PostTask(fxl::MakeCopyable(
          [ this, stream_index, packet = std::move(packet) ]() mutable {
            ActiveMultistreamSourceStage* stage_ptr = stage();
            if (stage_ptr) {
              stage_ptr->SupplyPacket(stream_index, std::move(packet));
            }
          }));
    }
  }
}

bool FfmpegDemuxImpl::Wait(bool* packet_requested,
                           int64_t* seek_position,
                           SeekCallback* seek_callback)
    // TODO(US-452): Re-enable thread safety analysis once unique_lock
    // has proper annotations.
    FXL_NO_THREAD_SAFETY_ANALYSIS {
  std::unique_lock<std::mutex> locker(mutex_);
  while (!packet_requested_ && !terminating_ && seek_position_ == kNotSeeking) {
    condition_variable_.wait(locker);
  }

  if (terminating_) {
    return false;
  }

  *packet_requested = packet_requested_;
  packet_requested_ = false;

  *seek_position = seek_position_;
  seek_position_ = kNotSeeking;

  seek_callback_.swap(*seek_callback);
  return true;
}

PacketPtr FfmpegDemuxImpl::PullPacket(size_t* stream_index_out) {
  FXL_DCHECK(stream_index_out);

  if (next_stream_to_end_ != -1) {
    // We're producing end-of-stream packets for all the streams.
    return PullEndOfStreamPacket(stream_index_out);
  }

  ffmpeg::AvPacketPtr av_packet = ffmpeg::AvPacket::Create();

  av_packet->data = nullptr;
  av_packet->size = 0;

  if (av_read_frame(format_context_.get(), av_packet.get()) < 0) {
    // End of stream. Start producing end-of-stream packets for all the streams.
    next_stream_to_end_ = 0;
    return PullEndOfStreamPacket(stream_index_out);
  }

  *stream_index_out = static_cast<size_t>(av_packet->stream_index);
  // TODO(dalesat): What if the packet has no PTS or duration?
  next_pts_ = av_packet->pts + av_packet->duration;
  // TODO(dalesat): Implement packet side data.
  FXL_DCHECK(av_packet->side_data == nullptr) << "side data not implemented";
  FXL_DCHECK(av_packet->side_data_elems == 0);

  return DemuxPacket::Create(std::move(av_packet),
                             streams_[*stream_index_out]->pts_rate());
}

PacketPtr FfmpegDemuxImpl::PullEndOfStreamPacket(size_t* stream_index_out) {
  FXL_DCHECK(next_stream_to_end_ >= 0);

  if (static_cast<std::size_t>(next_stream_to_end_) >= streams_.size()) {
    FXL_DCHECK(false) << "PullPacket called after all streams have ended";
    return nullptr;
  }

  *stream_index_out = next_stream_to_end_++;
  return Packet::CreateEndOfStream(next_pts_,
                                   streams_[*stream_index_out]->pts_rate());
}

void FfmpegDemuxImpl::CopyMetadata(AVDictionary* source,
                                   std::map<std::string, std::string>& map) {
  if (source == nullptr) {
    return;
  }

  for (AVDictionaryEntry* entry =
           av_dict_get(source, "", nullptr, AV_DICT_IGNORE_SUFFIX);
       entry != nullptr;
       entry = av_dict_get(source, "", entry, AV_DICT_IGNORE_SUFFIX)) {
    if (map.find(entry->key) == map.end()) {
      map.emplace(entry->key, entry->value);
    }
  }
}

void FfmpegDemuxImpl::SendStatus() {
  if (!status_callback_) {
    return;
  }

  std::unique_ptr<Metadata> metadata;
  std::string problem_type;
  std::string problem_details;

  {
    std::lock_guard<std::mutex> locker(mutex_);
    metadata = SafeClone(metadata_);
    problem_type = problem_type_;
    problem_details = problem_details_;
  }

  status_callback_(metadata, problem_type, problem_details);
}

void FfmpegDemuxImpl::ReportProblem(const std::string& type,
                                    const std::string& details) {
  {
    std::lock_guard<std::mutex> locker(mutex_);
    problem_type_ = type;
    problem_details_ = details;
  }
  task_runner_->PostTask([this]() { SendStatus(); });
}

FfmpegDemuxImpl::FfmpegDemuxStream::FfmpegDemuxStream(
    const AVFormatContext& format_context,
    size_t index)
    : stream_(format_context.streams[index]), index_(index) {
  stream_type_ = AvCodecContext::GetStreamType(*stream_);
  pts_rate_ = TimelineRate(stream_->time_base.den, stream_->time_base.num);
}

FfmpegDemuxImpl::FfmpegDemuxStream::~FfmpegDemuxStream() {}

size_t FfmpegDemuxImpl::FfmpegDemuxStream::index() const {
  return index_;
}

std::unique_ptr<StreamType> FfmpegDemuxImpl::FfmpegDemuxStream::stream_type()
    const {
  return SafeClone(stream_type_);
}

TimelineRate FfmpegDemuxImpl::FfmpegDemuxStream::pts_rate() const {
  return pts_rate_;
}

}  // namespace media
