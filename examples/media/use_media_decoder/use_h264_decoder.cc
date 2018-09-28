// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "use_h264_decoder.h"

#include "codec_client.h"
#include "frame_sink.h"
#include "util.h"

#include <garnet/lib/media/raw_video_writer/raw_video_writer.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fit/defer.h>
#include <lib/fxl/arraysize.h>
#include <lib/fxl/logging.h>

#include <stdint.h>
#include <string.h>
#include <thread>

namespace {

constexpr bool kRawVideoWriterEnabled = true;

// This example only has one stream_lifetime_ordinal which is 1.
//
// TODO(dustingreen): actually re-use the Codec instance for at least one more
// stream, even if it's just to decode the same data again.
constexpr uint64_t kStreamLifetimeOrdinal = 1;

// Scenic ImagePipe doesn't allow image_id 0, so offset by this much.
constexpr uint32_t kFirstValidImageId = 1;

constexpr uint8_t kLongStartCodeArray[] = {0x00, 0x00, 0x00, 0x01};
constexpr uint8_t kShortStartCodeArray[] = {0x00, 0x00, 0x01};

// If readable_bytes is 0, that's considered a "start code", to allow the caller
// to terminate a NAL the same way regardless of whether another start code is
// found or the end of the buffer is found.
//
// ptr has readable_bytes of data - the function only evaluates whether there is
// a start code at the begining of the data at ptr.
//
// readable_bytes - the caller indicates how many bytes are readable starting at
// ptr.
//
// *start_code_size_bytes will have length of the start code in bytes when the
// function returns true - unchanged otherwise.  Normally this would be 3 or 4,
// but a 0 is possible if readable_bytes is 0.
bool is_start_code(uint8_t* ptr, size_t readable_bytes,
                   size_t* start_code_size_bytes_out) {
  if (readable_bytes == 0) {
    *start_code_size_bytes_out = 0;
    return true;
  }
  if (readable_bytes >= 4) {
    if (!memcmp(ptr, kLongStartCodeArray, sizeof(kLongStartCodeArray))) {
      *start_code_size_bytes_out = 4;
      return true;
    }
  }
  if (readable_bytes >= 3) {
    if (!memcmp(ptr, kShortStartCodeArray, sizeof(kShortStartCodeArray))) {
      *start_code_size_bytes_out = 3;
      return true;
    }
  }
  return false;
}

// Test-only.  Not for production use.  Caller must ensure there are at least 5
// bytes at nal_unit.
uint8_t GetNalUnitType(const uint8_t* nal_unit) {
  // Also works with 4-byte startcodes.
  static const uint8_t start_code[3] = {0, 0, 1};
  uint8_t* next_start = static_cast<uint8_t*>(memmem(nal_unit, 5, start_code,
                                                     sizeof(start_code))) +
                        sizeof(start_code);
  return *next_start & 0xf;
}

struct __attribute__((__packed__)) IvfHeader {
  uint32_t signature;
  uint16_t version;
  uint16_t header_length;
  uint32_t fourcc;
  uint16_t width;
  uint16_t height;
  uint32_t frame_rate;
  uint32_t time_scale;
  uint32_t frame_count;
  uint32_t unused;
};

struct __attribute__((__packed__)) IvfFrameHeader {
  uint32_t size_bytes;
  uint64_t presentation_timestamp;
};

enum class Format {
  kH264,
  kVp9,
};

}  // namespace

void QueueH264Frames(CodecClient* codec_client, uint8_t* input_bytes,
                     size_t input_size) {
  // We assign fake PTS values starting at 0 partly to verify that 0 is
  // treated as a valid PTS.
  uint64_t input_frame_pts_counter = 0;
  // Raw .h264 has start code 00 00 01 or 00 00 00 01 before each NAL, and
  // the start codes don't alias in the middle of NALs, so we just scan
  // for NALs and send them in to the decoder.
  auto queue_access_unit = [&codec_client, &input_bytes,
                            &input_frame_pts_counter](uint8_t* bytes,
                                                      size_t byte_count) {
    size_t bytes_so_far = 0;
    // printf("queuing offset: %ld byte_count: %zu\n", bytes -
    // input_bytes.get(), byte_count);
    while (bytes_so_far != byte_count) {
      std::unique_ptr<fuchsia::mediacodec::CodecPacket> packet =
          codec_client->BlockingGetFreeInputPacket();
      const CodecBuffer& buffer =
          codec_client->GetInputBufferByIndex(packet->header.packet_index);
      size_t bytes_to_copy =
          std::min(byte_count - bytes_so_far, buffer.size_bytes());
      packet->stream_lifetime_ordinal = kStreamLifetimeOrdinal;
      packet->start_offset = 0;
      packet->valid_length_bytes = bytes_to_copy;

      packet->has_timestamp_ish = false;
      packet->timestamp_ish = 0;
      if (bytes_so_far == 0) {
        uint8_t nal_unit_type = GetNalUnitType(bytes);
        if (nal_unit_type == 1 || nal_unit_type == 5) {
          packet->has_timestamp_ish = true;
          packet->timestamp_ish = input_frame_pts_counter++;
        }
      }

      packet->start_access_unit = (bytes_so_far == 0);
      packet->known_end_access_unit =
          (bytes_so_far + bytes_to_copy == byte_count);
      memcpy(buffer.base(), bytes + bytes_so_far, bytes_to_copy);
      codec_client->QueueInputPacket(std::move(packet));
      bytes_so_far += bytes_to_copy;
    }
  };
  for (size_t i = 0; i < input_size;) {
    size_t start_code_size_bytes;
    if (!is_start_code(&input_bytes[i], input_size - i,
                       &start_code_size_bytes)) {
      if (i == 0) {
        Exit(
            "Didn't find a start code at the start of the file, and this "
            "example doesn't scan forward (for now).");
      } else {
        Exit(
            "Fell out of sync somehow - previous NAL offset + previous "
            "NAL length not a start code.");
      }
    }
    if (i + start_code_size_bytes == input_size) {
      Exit("Start code at end of file unexpected");
    }
    size_t nal_start_offset = i + start_code_size_bytes;
    // Scan for end of NAL.  The end of NAL can be because we're out of
    // data, or because we hit another start code.
    size_t find_end_iter = nal_start_offset;
    size_t ignore_start_code_size_bytes;
    while (find_end_iter <= input_size &&
           !is_start_code(&input_bytes[find_end_iter],
                          input_size - find_end_iter,
                          &ignore_start_code_size_bytes)) {
      find_end_iter++;
    }
    FXL_DCHECK(find_end_iter <= input_size);
    if (find_end_iter == nal_start_offset) {
      Exit("Two adjacent start codes unexpected.");
    }
    FXL_DCHECK(find_end_iter > nal_start_offset);
    size_t nal_length = find_end_iter - nal_start_offset;
    queue_access_unit(&input_bytes[i], start_code_size_bytes + nal_length);
    // start code + NAL payload
    i += start_code_size_bytes + nal_length;
  }

  // Send through QueueInputEndOfStream().
  codec_client->QueueInputEndOfStream(kStreamLifetimeOrdinal);
  // input thread done
}
void QueueVp9Frames(CodecClient* codec_client, uint8_t* input_bytes,
                    size_t input_size) {
  auto queue_access_unit = [&codec_client, &input_bytes](uint8_t* bytes,
                                                         size_t byte_count,
                                                         uint32_t frame_pts) {
    std::unique_ptr<fuchsia::mediacodec::CodecPacket> packet =
        codec_client->BlockingGetFreeInputPacket();
    const CodecBuffer& buffer =
        codec_client->GetInputBufferByIndex(packet->header.packet_index);
    // VP9 decoder doesn't yet support splitting access units into multiple
    // packets.
    FXL_DCHECK(byte_count <= buffer.size_bytes());
    packet->stream_lifetime_ordinal = kStreamLifetimeOrdinal;
    packet->start_offset = 0;
    packet->valid_length_bytes = byte_count;

    packet->has_timestamp_ish = true;
    packet->timestamp_ish = frame_pts;

    packet->start_access_unit = true;
    packet->known_end_access_unit = true;
    memcpy(buffer.base(), bytes, byte_count);
    codec_client->QueueInputPacket(std::move(packet));
  };
  IvfHeader* header = (IvfHeader*)&input_bytes[0];
  for (size_t i = header->header_length; i < input_size;) {
    if (i + sizeof(IvfFrameHeader) > input_size)
      Exit("Frame header truncated.");
    IvfFrameHeader* frame_header = (IvfFrameHeader*)&input_bytes[i];
    if (i + sizeof(IvfFrameHeader) + frame_header->size_bytes > input_size)
      Exit("Frame truncated.");
    queue_access_unit(&input_bytes[i + sizeof(IvfFrameHeader)],
                      frame_header->size_bytes,
                      frame_header->presentation_timestamp);
    i += sizeof(IvfFrameHeader) + frame_header->size_bytes;
  }

  // Send through QueueInputEndOfStream().
  codec_client->QueueInputEndOfStream(kStreamLifetimeOrdinal);
  // input thread done
}

static void use_video_decoder(
    async::Loop* main_loop, fuchsia::mediacodec::CodecFactoryPtr codec_factory,
    Format format, const std::string& input_file,
    const std::string& output_file, uint8_t md_out[SHA256_DIGEST_LENGTH],
    std::vector<std::pair<bool, uint64_t>>* timestamps_out,
    FrameSink* frame_sink) {
  VLOGF("use_h264_decoder()\n");
  FXL_DCHECK(!timestamps_out || timestamps_out->empty());
  memset(md_out, 0, SHA256_DIGEST_LENGTH);
  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
  loop.StartThread("use_h264_decoder_loop");

  // payload data for bear.h264 is 00 00 00 01 start code before each NAL, with
  // SPS / PPS NALs and also frame NALs.  We deliver to Codec NAL-by-NAL without
  // the start code, since the Codec packet
  VLOGF("reading h264 file...\n");
  size_t input_size;
  std::unique_ptr<uint8_t[]> input_bytes =
      read_whole_file(input_file.c_str(), &input_size);
  VLOGF("done reading h264 file.\n");

  // Since the .h264 file has SPS + PPS NALs in addition to frame NALs, we don't
  // use codec_oob_bytes for this stream.
  //
  // TODO(dustingreen): Determine for .mp4 or similar which don't have SPS / PPS
  // in band whether .mp4 provides ongoing OOB data, or just at the start, and
  // document in codec.fidl how that's to be handled.

  VLOGF("before CodecClient::CodecClient()...\n");
  CodecClient codec_client(&loop);

  const char* mime_type;
  switch (format) {
    case Format::kH264:
      mime_type = "video/h264";
      break;

    case Format::kVp9:
      mime_type = "video/vp9";
      break;
  }

  async::PostTask(
      main_loop->dispatcher(),
      [&codec_factory, codec_client_request = codec_client.GetTheRequestOnce(),
       mime_type]() mutable {
        VLOGF("before codec_factory->CreateDecoder() (async)\n");
        codec_factory->CreateDecoder(
            fuchsia::mediacodec::CreateDecoder_Params{
                .input_details.format_details_version_ordinal = 0,
                .input_details.mime_type = mime_type,
                // This is required for timestamp_ish values to transit the
                // Codec.
                .promise_separate_access_units_on_input = true,
            },
            std::move(codec_client_request));
      });

  VLOGF("before codec_client.Start()...\n");
  // This does a Sync(), so after this we can drop the CodecFactory without it
  // potentially cancelling our Codec create.
  codec_client.Start();

  // We don't need the CodecFactory any more, and at this point any Codec
  // creation errors have had a chance to arrive via the
  // codec_factory.set_error_handler() lambda.
  //
  // Unbind() is only safe to call on the interfaces's dispatcher thread.  We
  // also want to block the current thread until this is done, to avoid
  // codec_factory potentially disapearing before this posted work finishes.
  std::mutex unbind_mutex;
  std::condition_variable unbind_done_condition;
  bool unbind_done = false;
  async::PostTask(
      main_loop->dispatcher(),
      [&codec_factory, &unbind_mutex, &unbind_done, &unbind_done_condition] {
        codec_factory.Unbind();
        {  // scope lock
          std::lock_guard<std::mutex> lock(unbind_mutex);
          unbind_done = true;
        }  // ~lock
        unbind_done_condition.notify_all();
        // All of codec_factory, unbind_mutex, unbind_done,
        // unbind_done_condition are potentially gone by this point.
      });
  {  // scope lock
    std::unique_lock<std::mutex> lock(unbind_mutex);
    while (!unbind_done) {
      unbind_done_condition.wait(lock);
    }
  }  // ~lock
  FXL_DCHECK(unbind_done);

  VLOGF("before starting in_thread...\n");
  std::unique_ptr<std::thread> in_thread = std::make_unique<std::thread>(
      [&codec_client, &input_bytes, input_size, format]() {
        switch (format) {
          case Format::kH264:
            QueueH264Frames(&codec_client, input_bytes.get(), input_size);
            break;

          case Format::kVp9:
            QueueVp9Frames(&codec_client, input_bytes.get(), input_size);
            break;
        }
      });

  // Separate thread to process the output.
  //
  // codec_client outlives the thread (and for separate reasons below, all the
  // frame_sink activity started by out_thread).
  std::unique_ptr<std::thread> out_thread = std::make_unique<
      std::thread>([main_loop, &codec_client, output_file, md_out,
                    &timestamps_out, frame_sink]() {
    // The codec_client lock_ is not held for long durations in here, which is
    // good since we're using this thread to do things like write to an output
    // file.
    media::RawVideoWriter<kRawVideoWriterEnabled> raw_video_writer(
        output_file.c_str());
    SHA256_CTX sha256_ctx;
    SHA256_Init(&sha256_ctx);
    // We allow the server to send multiple output format updates if it wants;
    // see implementation of BlockingGetEmittedOutput() which will hide
    // multiple configs before the first packet from this code.
    //
    // In this example, we only deal with one output format once we start seeing
    // stream output data show up, since our raw_video_writer is only really
    // meant to store one format per file.
    std::shared_ptr<const fuchsia::mediacodec::CodecOutputConfig> stream_config;
    const fuchsia::mediacodec::VideoUncompressedFormat* raw = nullptr;
    while (true) {
      std::unique_ptr<CodecOutput> output =
          codec_client.BlockingGetEmittedOutput();
      if (output->stream_lifetime_ordinal() != kStreamLifetimeOrdinal) {
        Exit(
            "server emitted a stream_lifetime_ordinal that client didn't set "
            "on any input");
      }
      if (output->end_of_stream()) {
        VLOGF("output end_of_stream() - done with output\n");
        // Just "break;" would be more fragile under code modification.
        goto end_of_output;
      }

      const fuchsia::mediacodec::CodecPacket& packet = output->packet();
      // cleanup can run on any thread, and codec_client.RecycleOutputPacket()
      // is ok with that.  In addition, cleanup can run after codec_client is
      // gone, since we don't block return from use_h264_decoder() on Scenic
      // actually freeing up all previously-queued frames.
      auto cleanup = fit::defer(
          [&codec_client, packet_header = fidl::Clone(packet.header)] {
            // Using an auto call for this helps avoid losing track of the
            // output_buffer.
            //
            // If the omx_state_ or omx_state_desired_ isn't correct,
            // UseOutputBuffer() will fail.  The only way that can happen here
            // is if the OMX codec transitioned states unilaterally without any
            // set state command, so if that occurs, exit.
            codec_client.RecycleOutputPacket(std::move(packet_header));
          });
      std::shared_ptr<const fuchsia::mediacodec::CodecOutputConfig> config =
          output->config();
      // This will remain live long enough because this thread is the only
      // thread that re-allocates output buffers.
      const CodecBuffer& buffer =
          codec_client.GetOutputBufferByIndex(packet.header.packet_index);

      if (stream_config &&
          (config->format_details.format_details_version_ordinal !=
           stream_config->format_details.format_details_version_ordinal)) {
        Exit(
            "codec server unexpectedly changed output format mid-stream - "
            "unexpected for this stream");
      }

      if (packet.valid_length_bytes == 0) {
        // The server should not generate any empty packets.
        Exit("broken server sent empty packet");
      }

      // We have a non-empty packet of the stream.

      if (!stream_config) {
        // Every output has a config.  This happens exactly once.
        stream_config = config;
        const fuchsia::mediacodec::CodecFormatDetails& format =
            stream_config->format_details;
        if (!format.domain->is_video()) {
          Exit("!format.domain.is_video()");
        }
        const fuchsia::mediacodec::VideoFormat& video_format =
            format.domain->video();
        if (!video_format.is_uncompressed()) {
          Exit("!video.is_uncompressed()");
        }
        raw = &video_format.uncompressed();
        if (raw->fourcc != make_fourcc('N', 'V', '1', '2')) {
          Exit("fourcc != NV12");
        }
        size_t y_size =
            raw->primary_height_pixels * raw->primary_line_stride_bytes;
        if (raw->secondary_start_offset < y_size) {
          Exit("raw.secondary_start_offset < y_size");
        }
        // NV12 requires UV be same line stride as Y.
        size_t total_size =
            raw->secondary_start_offset +
            raw->primary_height_pixels / 2 * raw->primary_line_stride_bytes;
        if (packet.valid_length_bytes < total_size) {
          Exit("packet.valid_length_bytes < total_size");
        }
        if (!frame_sink) {
          SHA256_Update_VideoParameters(&sha256_ctx, *raw);
        }
      }

      if (!output_file.empty()) {
        raw_video_writer.WriteNv12(
            raw->primary_width_pixels, raw->primary_height_pixels,
            raw->primary_line_stride_bytes,
            buffer.base() + packet.start_offset + raw->primary_start_offset,
            raw->secondary_start_offset - raw->primary_start_offset);
      }

      // PTS values are separately verified by use_h264_decoder_test since it'll
      // be nice to know separately if they're broken and how vs. frame format
      // and frame pixel data being broken, especially if there's just one
      // broken run that can't easily be reproduced.
      if (timestamps_out) {
        timestamps_out->emplace_back(
            std::make_pair(packet.has_timestamp_ish, packet.timestamp_ish));
      }

      if (!frame_sink) {
        // Y
        uint8_t* y_src =
            buffer.base() + packet.start_offset + raw->primary_start_offset;
        for (uint32_t y_iter = 0; y_iter < raw->primary_height_pixels;
             y_iter++) {
          SHA256_Update(&sha256_ctx, y_src, raw->primary_width_pixels);
          y_src += raw->primary_line_stride_bytes;
        }
        // UV
        uint8_t* uv_src =
            buffer.base() + packet.start_offset + raw->secondary_start_offset;
        for (uint32_t uv_iter = 0; uv_iter < raw->primary_height_pixels / 2;
             uv_iter++) {
          // NV12 requires eacy UV line be same width as a Y line, and same
          // stride as a Y line.
          SHA256_Update(&sha256_ctx, uv_src, raw->primary_width_pixels);
          uv_src += raw->primary_line_stride_bytes;
        }
      }

      if (frame_sink) {
        async::PostTask(
            main_loop->dispatcher(),
            [frame_sink,
             image_id = packet.header.packet_index + kFirstValidImageId,
             &vmo = buffer.vmo(),
             vmo_offset = buffer.vmo_offset() + packet.start_offset +
                          raw->primary_start_offset,
             config, cleanup = std::move(cleanup)]() mutable {
              frame_sink->PutFrame(image_id, vmo, vmo_offset, config,
                                   [cleanup = std::move(cleanup)] {
                                     // The ~cleanup can run on any thread (the
                                     // current thread is main_loop's thread),
                                     // and codec_client is ok with that
                                     // (because it switches over to |loop|'s
                                     // thread before sending a Codec message).
                                     //
                                     // ~cleanup
                                   });
            });
      }
      // If we didn't std::move(cleanup) before here, then ~cleanup runs here.
    }
  end_of_output:;
    if (!SHA256_Final(md_out, &sha256_ctx)) {
      assert(false);
    }
    printf("output thread done\n");
    // output thread done
    // ~raw_video_writer
  });

  // decode for a bit...  in_thread, loop, out_thread, and the codec itself are
  // taking care of it.

  // First wait for the input thread to be done feeding input data.  Before the
  // in_thread terminates, it'll have sent in a last empty EOS input buffer.
  VLOGF("before in_thread->join()...\n");
  in_thread->join();
  VLOGF("after in_thread->join()\n");

  // The EOS queued as an input buffer should cause the codec to output an EOS
  // output buffer, at which point out_thread should terminate, after it has
  // finalized the output file.
  VLOGF("before out_thread->join()...\n");
  out_thread->join();
  VLOGF("after out_thread->join()\n");

  // We wait for frame_sink to return all the frames for these reasons:
  //   * As of this writing, some noisy-in-the-log things can happen in Scenic
  //     if we don't.
  //   * We don't want to cancel display of any frames, because we want to see
  //     the frames on the screen.
  //   * We don't want the |cleanup| to run after codec_client is gone since the
  //     |cleanup| calls codec_client.
  //   * It's easier to grok if activity started by use_h264_decoder() is done
  //     by the time use_h264_decoder() returns, given use_h264_decoder()'s role
  //     as an overall sequencer.
  if (frame_sink) {
    // TODO(dustingreen): Make this less hacky - currently we sleep 10 seconds
    // to give Scenic a chance to display anything.  Despite this, we don't see
    // many frames displayed - TBD why not (it's not the cost of SW-based
    // YUV-to-RGB conversion, since removing most of that cost doesn't change
    // # of frames displayed).
    FXL_LOG(INFO) << "sleeping 10 seconds...";
    zx_nanosleep(zx_deadline_after(ZX_SEC(10)));
    FXL_LOG(INFO) << "done sleeping.";

    std::mutex frames_done_lock;
    bool frames_done = false;
    std::condition_variable frames_done_condition;
    fit::closure on_frames_returned = [&frames_done_lock, &frames_done,
                                       &frames_done_condition] {
      {  // scope lock
        std::lock_guard<std::mutex> lock(frames_done_lock);
        frames_done = true;
        // The notify while still under the lock prevents any possibility of
        // frames_done_condition being gone too soon.
        frames_done_condition.notify_all();
        // Don't touch the captures beyond this point.
      }  // ~lock
    };
    async::PostTask(main_loop->dispatcher(),
                    [frame_sink, on_frames_returned =
                                     std::move(on_frames_returned)]() mutable {
                      frame_sink->PutEndOfStreamThenWaitForFramesReturnedAsync(
                          std::move(on_frames_returned));
                    });
    // The just-posted wait will set frames_done using the main_loop_'s thread,
    // which is not this thread.
    FXL_LOG(INFO) << "waiting for all frames to be returned from Scenic...";
    {  // scope lock
      std::unique_lock<std::mutex> lock(frames_done_lock);
      while (!frames_done) {
        frames_done_condition.wait(lock);
      }
    }  // ~lock
    FXL_LOG(INFO) << "all frames have been returned from Scenic";
    // Now we know that there are zero frames in frame_sink, including zero
    // frame cleanup(s) in-flight (in the sense of a pending/running cleanup
    // that's touching codec_client to post any new work.  Work already posted
    // via codec_client can still be in flight.  See below.)
  }

  // Because CodecClient posted work to the loop which captured the CodecClient
  // as "this", it's important that we ensure that all such work is done trying
  // to run before we delete CodecClient.  We need to know that the work posted
  // using PostSerial() won't be trying to touch the channel or pointers that
  // are owned by CodecClient before we close the channel or destruct
  // CodecClient (which happens before ~loop).
  //
  // We call loop.Quit();loop.JoinThreads(); before codec_client.Stop() because
  // there can be at least a RecycleOutputPacket() still working its way toward
  // the Codec (via the loop) at this point, so doing
  // loop.Quit();loop.JoinThreads(); first avoids potential FIDL message send
  // errors.  We're done decoding so we don't care whether any remaining queued
  // messages toward the codec actually reach the codec.
  //
  // We use loop.Quit();loop.JoinThreads(); instead of loop.Shutdown() because
  // we don't want the Shutdown() side-effect of failing the channel bindings.
  // The Shutdown() will happen later.
  //
  // By ensuring that the loop is done running code before closing the channel
  // (or loop.Shutdown()), we can close the channel cleanly and avoid mitigation
  // of expected normal channel closure (or loop.Shutdown()) in any code that
  // runs on the loop.  This way, unexpected channel failure is the only case to
  // worry about.
  VLOGF("before loop.Quit()\n");
  loop.Quit();
  VLOGF("before loop.JoinThreads()...\n");
  loop.JoinThreads();
  VLOGF("after loop.JoinThreads()\n");

  // Close the channel explicitly (just so we can more easily print messages
  // before and after vs. ~codec_client).
  VLOGF("before codec_client stop...\n");
  codec_client.Stop();
  VLOGF("after codec_client stop.\n");

  // loop.Shutdown() the rest of the way explicitly (just so we can more easily
  // print messages before and after vs. ~loop).  If we did this before
  // codec_client.Stop() it would cause the channel bindings to fail because
  // async waits are failed as cancelled during Shutdown().
  VLOGF("before loop.Shutdown()...\n");
  loop.Shutdown();
  VLOGF("after loop.Shutdown()\n");

  // The FIDL loop isn't running any more and the channels are closed.  There
  // are no other threads left that were started by this function.  We can just
  // delete stuff now.

  // success
  // ~codec_client
  // ~loop
  // ~codec_factory
  return;
}

void use_h264_decoder(async::Loop* main_loop,
                      fuchsia::mediacodec::CodecFactoryPtr codec_factory,
                      const std::string& input_file,
                      const std::string& output_file,
                      uint8_t md_out[SHA256_DIGEST_LENGTH],
                      std::vector<std::pair<bool, uint64_t>>* timestamps_out,
                      FrameSink* frame_sink) {
  use_video_decoder(main_loop, std::move(codec_factory), Format::kH264,
                    input_file, output_file, md_out, timestamps_out,
                    frame_sink);
}

void use_vp9_decoder(async::Loop* main_loop,
                     fuchsia::mediacodec::CodecFactoryPtr codec_factory,
                     const std::string& input_file,
                     const std::string& output_file,
                     uint8_t md_out[SHA256_DIGEST_LENGTH],
                     std::vector<std::pair<bool, uint64_t>>* timestamps_out,
                     FrameSink* frame_sink) {
  use_video_decoder(main_loop, std::move(codec_factory), Format::kVp9,
                    input_file, output_file, md_out, timestamps_out,
                    frame_sink);
}
