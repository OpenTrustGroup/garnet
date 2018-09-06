// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "amlogic-video.h"

#include <byteswap.h>

#include "gtest/gtest.h"
#include "hevcdec.h"
#include "lib/fxl/logging.h"
#include "tests/test_support.h"
#include "vp9_decoder.h"

#include "macros.h"
#include "pts_manager.h"

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

std::vector<uint32_t> TryParseSuperframeHeader(const uint8_t* data,
                                               uint32_t frame_size) {
  std::vector<uint32_t> frame_sizes;
  if (frame_size < 1)
    return frame_sizes;
  uint8_t superframe_header = data[frame_size - 1];

  // Split out superframes into separate frames - see
  // https://storage.googleapis.com/downloads.webmproject.org/docs/vp9/vp9-bitstream-specification-v0.6-20160331-draft.pdf
  // Annex B.
  if ((superframe_header & 0xc0) != 0xc0)
    return frame_sizes;
  uint8_t bytes_per_framesize = ((superframe_header >> 3) & 3) + 1;
  uint8_t superframe_count = (superframe_header & 7) + 1;
  uint32_t superframe_index_size = 2 + bytes_per_framesize * superframe_count;
  if (superframe_index_size > frame_size)
    return frame_sizes;
  if (data[frame_size - superframe_index_size] != superframe_header) {
    return frame_sizes;
  }
  const uint8_t* index_data = &data[frame_size - superframe_index_size + 1];
  uint32_t total_size = 0;
  for (uint32_t i = 0; i < superframe_count; i++) {
    uint32_t sub_frame_size;
    switch (bytes_per_framesize) {
      case 1:
        sub_frame_size = index_data[i];
        break;
      case 2:
        sub_frame_size = reinterpret_cast<const uint16_t*>(index_data)[i];
        break;
      case 4:
        sub_frame_size = reinterpret_cast<const uint32_t*>(index_data)[i];
        break;
      default:
        DECODE_ERROR("Unsupported bytes_per_framesize: %d\n",
                     bytes_per_framesize);
        frame_sizes.clear();
        return frame_sizes;
    }
    total_size += sub_frame_size;
    if (total_size > frame_size) {
      DECODE_ERROR("Total superframe size too large: %u > %u\n", total_size,
                   frame_size);
      frame_sizes.clear();
      return frame_sizes;
    }
    frame_sizes.push_back(sub_frame_size);
  }
  return frame_sizes;
}

void SplitSuperframe(const uint8_t* data, uint32_t frame_size,
                     std::vector<uint8_t>* output_vector) {
  std::vector<uint32_t> frame_sizes =
      TryParseSuperframeHeader(data, frame_size);

  if (frame_sizes.empty())
    frame_sizes.push_back(frame_size);
  uint32_t frame_offset = 0;
  uint32_t total_frame_bytes = 0;
  for (auto& size : frame_sizes) {
    total_frame_bytes += size;
  }
  const uint32_t kOutputHeaderSize = 16;
  uint32_t output_offset = output_vector->size();
  // This can be called multiple times on the same output_vector overall, but
  // should be amortized O(1), since resizing larger inserts elements at the end
  // and inserting elements at the end is amortized O(1) for std::vector.  Also,
  // for now this is for testing.
  output_vector->resize(output_offset + total_frame_bytes +
                        kOutputHeaderSize * frame_sizes.size());
  uint8_t* output = &(*output_vector)[output_offset];
  for (auto& size : frame_sizes) {
    FXL_DCHECK(output + 16 - output_vector->data() <=
               static_cast<int64_t>(output_vector->size()));
    *reinterpret_cast<uint32_t*>(output) = bswap_32(size + 4);
    output += 4;
    *reinterpret_cast<uint32_t*>(output) = ~bswap_32(size + 4);
    output += 4;
    *output++ = 0;
    *output++ = 0;
    *output++ = 0;
    *output++ = 1;
    *output++ = 'A';
    *output++ = 'M';
    *output++ = 'L';
    *output++ = 'V';

    FXL_DCHECK(output + size - output_vector->data() <=
               static_cast<int64_t>(output_vector->size()));
    memcpy(output, &data[frame_offset], size);
    output += size;
    frame_offset += size;
  }
  FXL_DCHECK(output - output_vector->data() ==
             static_cast<int64_t>(output_vector->size()));
}

std::vector<uint8_t> ConvertIvfToAmlV(const uint8_t* data, uint32_t length) {
  uint32_t offset = sizeof(IvfHeader);
  std::vector<uint8_t> output_vector;
  while (offset < length) {
    auto frame_header = reinterpret_cast<const IvfFrameHeader*>(data + offset);
    uint32_t frame_size = frame_header->size_bytes;
    uint32_t data_offset = offset + sizeof(IvfFrameHeader);
    if (data_offset + frame_size > length) {
      DECODE_ERROR("Invalid IVF file, truncating\n");
      return output_vector;
    }

    SplitSuperframe(data + data_offset, frame_size, &output_vector);

    offset = data_offset + frame_size;
  }
  return output_vector;
}

struct FrameData {
  uint64_t presentation_timestamp;
  std::vector<uint8_t> data;
};
// Split IVF-level frames
std::vector<FrameData> ConvertIvfToAmlVFrames(const uint8_t* data,
                                              uint32_t length) {
  uint32_t offset = sizeof(IvfHeader);
  std::vector<FrameData> output_vector;
  while (offset < length) {
    auto frame_header = reinterpret_cast<const IvfFrameHeader*>(data + offset);
    uint32_t frame_size = frame_header->size_bytes;
    uint32_t data_offset = offset + sizeof(IvfFrameHeader);
    if (data_offset + frame_size > length) {
      DECODE_ERROR("Invalid IVF file, truncating\n");
      return output_vector;
    }

    FrameData frame_data;
    frame_data.presentation_timestamp = frame_header->presentation_timestamp;
    SplitSuperframe(data + data_offset, frame_size, &frame_data.data);
    output_vector.push_back(std::move(frame_data));

    offset = data_offset + frame_size;
  }
  return output_vector;
}

class TestFrameProvider : public Vp9Decoder::FrameDataProvider {
 public:
  TestFrameProvider(AmlogicVideo* video, bool multi_instance)
      : video_(video), multi_instance_(multi_instance) {}

  // Always claim that 50 more bytes are available. Due to the 16kB of padding
  // at the end this is always true.
  uint32_t GetInputDataSize() override { return 50; }

  // Called while the decoder lock is held.
  void FrameWasOutput() override FXL_NO_THREAD_SAFETY_ANALYSIS {
    DLOG("Resetting hardware\n");
    SaveCurrentInstanceState();
    if (multi_instance_) {
      DecoderInstance* other_instance = &video_->decoder_instances_.back();
      // Only try to execute from the other instance if it hasn't decoded all
      // its data yet.
      if (!other_instance->input_context() ||
          (other_instance->input_context()->processed_video <
           other_instance->stream_buffer()->data_size())) {
        // Move current instance to the back of the list.
        video_->decoder_instances_.splice(video_->decoder_instances_.end(),
                                          video_->decoder_instances_,
                                          video_->decoder_instances_.begin());
      }
    }
    RestoreInstanceState();
  }

 private:
  // Called while the decoder lock is held.
  void SaveCurrentInstanceState() FXL_NO_THREAD_SAFETY_ANALYSIS {
    DecoderInstance* current_instance = &video_->decoder_instances_.front();
    // FrameWasOutput() is called during handling of kVp9CommandNalDecodeDone on
    // the interrupt thread, which means the decoder HW is currently paused,
    // which means it's ok to save the state before the stop+wait (without any
    // explicit pause before the save here).  The decoder HW remains paused
    // after the save, and makes no further progress until later after the
    // restore.
    if (!current_instance->input_context()) {
      current_instance->InitializeInputContext();
      EXPECT_EQ(ZX_OK, video_->core_->InitializeInputContext(
                           current_instance->input_context()));
    }
    video_->core_->SaveInputContext(current_instance->input_context());
    video_->core_->StopDecoding();
    video_->core()->WaitForIdle();
    // Completely power off the hardware to clear all the registers and ensure
    // the code correctly restores all the state.
    video_->core()->PowerOff();
  }

  // Called while the decoder lock is held.
  void RestoreInstanceState() FXL_NO_THREAD_SAFETY_ANALYSIS {
    DecoderInstance* current_instance = &video_->decoder_instances_.front();

    video_->video_decoder_ = current_instance->decoder();
    video_->stream_buffer_ = current_instance->stream_buffer();
    video_->core()->PowerOn();
    static_cast<Vp9Decoder*>(video_->video_decoder_)->InitializeHardware();
    if (!current_instance->input_context()) {
      video_->InitializeStreamInput(false);
      video_->core_->InitializeDirectInput();
      video_->core_->UpdateWritePointer(
          io_buffer_phys(video_->stream_buffer_->buffer()) +
          video_->stream_buffer_->data_size() +
          video_->stream_buffer_->padding_size());
    } else {
      video_->core_->RestoreInputContext(current_instance->input_context());
    }
    video_->core()->StartDecoding();
  }

  AmlogicVideo* video_;
  bool multi_instance_;
};

class TestVP9 {
 public:
  static void Decode(bool use_parser, const char* input_filename,
                     const char* filename) {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));

    video->pts_manager_ = std::make_unique<PtsManager>();

    video->core_ = std::make_unique<HevcDec>(video.get());
    video->core_->PowerOn();

    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->SetDefaultInstance(std::make_unique<Vp9Decoder>(
          video.get(), Vp9Decoder::InputType::kSingleStream));
    }
    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(use_parser, PAGE_SIZE));

    video->InitializeInterrupts();

    if (use_parser) {
      EXPECT_EQ(ZX_OK, video->InitializeEsParser());
    }

    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      EXPECT_EQ(ZX_OK, video->video_decoder_->Initialize());
    }

    uint32_t frame_count = 0;
    std::promise<void> wait_valid;
    bool frames_returned = false;  // Protected by video->video_decoder_lock_
    std::vector<std::shared_ptr<VideoFrame>> frames_to_return;
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->video_decoder_->SetFrameReadyNotifier(
          [&video, &frames_to_return, &frame_count, &wait_valid,
           &frames_returned](std::shared_ptr<VideoFrame> frame) {
            ++frame_count;
            DLOG("Got frame %d\n", frame_count);
            EXPECT_EQ(320u, frame->display_width);
            EXPECT_EQ(240u, frame->display_height);
#if DUMP_VIDEO_TO_FILE
            DumpVideoFrameToFile(frame, filename);
#endif
            if (frames_returned)
              ReturnFrame(video.get(), frame);
            else
              frames_to_return.push_back(frame);
            if (frame_count == 241)
              wait_valid.set_value();
          });
    }
    auto test_ivf = TestSupport::LoadFirmwareFile(input_filename);
    ASSERT_NE(nullptr, test_ivf);

    std::atomic<bool> stop_parsing(false);
    // Put on a separate thread because it needs video decoding to progress in
    // order to finish.
    auto parser = std::async([&video, use_parser, &test_ivf, &stop_parsing]() {
      auto aml_data = ConvertIvfToAmlV(test_ivf->ptr, test_ivf->size);
      if (use_parser) {
        EXPECT_EQ(ZX_OK, video->ParseVideo(aml_data.data(), aml_data.size()));
      } else {
        video->core_->InitializeDirectInput();
        uint32_t current_offset = 0;
        uint8_t* data = aml_data.data();
        while (!stop_parsing) {
          uint32_t processed_data;
          EXPECT_EQ(ZX_OK,
                    video->ProcessVideoNoParser(
                        data + current_offset, aml_data.size() - current_offset,
                        &processed_data));
          current_offset += processed_data;
          if (current_offset == aml_data.size())
            break;
          zx_nanosleep(zx_deadline_after(ZX_MSEC(15)));
        }
      }
    });

    zx_nanosleep(zx_deadline_after(ZX_SEC(1)));
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      for (auto frame : frames_to_return) {
        video->video_decoder_->ReturnFrame(frame);
      }
      frames_returned = true;
    }

    EXPECT_EQ(std::future_status::ready,
              wait_valid.get_future().wait_for(std::chrono::seconds(2)));

    stop_parsing = true;

    EXPECT_EQ(std::future_status::ready,
              parser.wait_for(std::chrono::seconds(1)));
    video.reset();
  }

  static void DecodePerFrame() {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));

    auto test_ivf =
        TestSupport::LoadFirmwareFile("video_test_data/test-25fps.vp9");
    ASSERT_NE(nullptr, test_ivf);
    video->pts_manager_ = std::make_unique<PtsManager>();

    video->core_ = std::make_unique<HevcDec>(video.get());
    video->core_->PowerOn();
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->SetDefaultInstance(std::make_unique<Vp9Decoder>(
          video.get(), Vp9Decoder::InputType::kSingleStream));
    }

    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(true, PAGE_SIZE));

    video->InitializeInterrupts();

    EXPECT_EQ(ZX_OK, video->InitializeEsParser());

    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      EXPECT_EQ(ZX_OK, video->video_decoder_->Initialize());
    }

    uint32_t frame_count = 0;
    std::promise<void> wait_valid;
    bool frames_returned = false;  // Protected by video->video_decoder_lock_
    std::vector<std::shared_ptr<VideoFrame>> frames_to_return;
    uint64_t next_pts = 0;
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->video_decoder_->SetFrameReadyNotifier(
          [&video, &frames_to_return, &frame_count, &wait_valid,
           &frames_returned, &next_pts](std::shared_ptr<VideoFrame> frame) {
            ++frame_count;
            DLOG("Got frame %d, pts: %ld\n", frame_count, frame->pts);
#if DUMP_VIDEO_TO_FILE
            DumpVideoFrameToFile(frame, filename);
#endif
            EXPECT_TRUE(frame->has_pts);
            // All frames are shown, so pts should be in order. Due to rounding,
            // pts may be 1 off.
            EXPECT_LE(next_pts, frame->pts);
            EXPECT_GE(next_pts + 1, frame->pts);

            // 25 fps video
            next_pts = frame->pts + 1000 / 25;
            ReturnFrame(video.get(), frame);
            if (frame_count == 241)
              wait_valid.set_value();
          });
    }

    // Put on a separate thread because it needs video decoding to progress in
    // order to finish.
    auto parser = std::async([&video, &test_ivf]() {
      auto aml_data = ConvertIvfToAmlVFrames(test_ivf->ptr, test_ivf->size);
      uint32_t stream_offset = 0;
      for (auto& data : aml_data) {
        video->pts_manager_->InsertPts(stream_offset,
                                       data.presentation_timestamp);
        EXPECT_EQ(ZX_OK, video->ParseVideo(data.data.data(), data.data.size()));
        stream_offset += data.data.size();
      }
    });

    EXPECT_EQ(std::future_status::ready,
              wait_valid.get_future().wait_for(std::chrono::seconds(2)));

    EXPECT_EQ(std::future_status::ready,
              parser.wait_for(std::chrono::seconds(1)));
    video.reset();
  }

  static void DecodeResetHardware(const char* filename) {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));

    video->pts_manager_ = std::make_unique<PtsManager>();

    video->core_ = std::make_unique<HevcDec>(video.get());
    video->core_->PowerOn();

    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->SetDefaultInstance(std::make_unique<Vp9Decoder>(
          video.get(), Vp9Decoder::InputType::kMultiStream));
    }
    // Don't use parser, because we need to be able to save and restore the read
    // and write pointers, which can't be done if the parser is using them as
    // well.
    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(false, 1024 * PAGE_SIZE));

    video->InitializeInterrupts();

    TestFrameProvider frame_provider(video.get(), false);
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      static_cast<Vp9Decoder*>(video->video_decoder_)
          ->SetFrameDataProvider(&frame_provider);
      EXPECT_EQ(ZX_OK, video->video_decoder_->Initialize());
    }

    uint32_t frame_count = 0;
    std::promise<void> wait_valid;
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->video_decoder_->SetFrameReadyNotifier(
          [&video, &frame_count, &wait_valid,
           filename](std::shared_ptr<VideoFrame> frame) {
            ++frame_count;
            DLOG("Got frame %d\n", frame_count);
#if DUMP_VIDEO_TO_FILE
            DumpVideoFrameToFile(frame.get(), filename);
#endif
            ReturnFrame(video.get(), frame);
            // Only 49 of the first 50 frames are shown.
            if (frame_count == 49)
              wait_valid.set_value();
          });
    }

    auto test_ivf =
        TestSupport::LoadFirmwareFile("video_test_data/test-25fps.vp9");
    ASSERT_NE(nullptr, test_ivf);
    auto aml_data = ConvertIvfToAmlVFrames(test_ivf->ptr, test_ivf->size);
    video->core_->InitializeDirectInput();
    // Only use the first 50 frames to save time.
    for (uint32_t i = 0; i < 50; i++) {
      EXPECT_EQ(ZX_OK, video->ProcessVideoNoParser(aml_data[i].data.data(),
                                                   aml_data[i].data.size()));
    }
    // Force all frames to be processed.
    uint8_t padding[16384] = {};
    EXPECT_EQ(ZX_OK, video->ProcessVideoNoParser(padding, sizeof(padding)));
    video->core()->StartDecoding();

    EXPECT_EQ(std::future_status::ready,
              wait_valid.get_future().wait_for(std::chrono::seconds(2)));

    video.reset();
  }

  static void DecodeMultiInstance() {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));

    video->pts_manager_ = std::make_unique<PtsManager>();

    video->core_ = std::make_unique<HevcDec>(video.get());
    video->core_->PowerOn();
    TestFrameProvider frame_provider(video.get(), true);

    for (uint32_t i = 0; i < 2; i++) {
      auto decoder = std::make_unique<Vp9Decoder>(
          video.get(), Vp9Decoder::InputType::kMultiStream);
      decoder->SetFrameDataProvider(&frame_provider);
      EXPECT_EQ(ZX_OK, decoder->InitializeBuffers());
      video->decoder_instances_.push_back(DecoderInstance(std::move(decoder)));
      StreamBuffer* buffer = video->decoder_instances_.back().stream_buffer();
      EXPECT_EQ(ZX_OK, video->AllocateStreamBuffer(buffer, PAGE_SIZE * 1024));
    }

    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->video_decoder_ = video->decoder_instances_.front().decoder();
      video->stream_buffer_ = video->decoder_instances_.front().stream_buffer();
      EXPECT_EQ(ZX_OK, static_cast<Vp9Decoder*>(video->video_decoder_)
                           ->InitializeHardware());
    }

    // Don't use parser, because we need to be able to save and restore the read
    // and write pointers, which can't be done if the parser is using them as
    // well.
    video->InitializeStreamInput(false);

    video->InitializeInterrupts();

    uint32_t frame_count = 0;
    std::promise<void> wait_valid;
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->video_decoder_->SetFrameReadyNotifier(
          [&video, &frame_count,
           &wait_valid](std::shared_ptr<VideoFrame> frame) {
            ++frame_count;
            DLOG("Got frame %d\n", frame_count);
            DLOG("Width: %d, height: %d\n", frame->width, frame->height);
#if DUMP_VIDEO_TO_FILE
            DumpVideoFrameToFile(frame.get(), "/tmp/bearmulti1.yuv");
#endif
            ReturnFrame(video.get(), frame);
            // Only 49 of the first 50 frames are shown.
            if (frame_count == 49)
              wait_valid.set_value();
          });
    }
    uint32_t frame_count1 = 0;
    std::promise<void> wait_valid1;
    video->decoder_instances_.back().decoder()->SetFrameReadyNotifier(
        [&video, &frame_count1,
         &wait_valid1](std::shared_ptr<VideoFrame> frame) {
          ++frame_count1;
          DLOG("Decoder 2 Got frame %d\n", frame_count1);
          EXPECT_EQ(320u, frame->display_width);
          EXPECT_EQ(240u, frame->display_height);
#if DUMP_VIDEO_TO_FILE
          DumpVideoFrameToFile(frame.get(), "/tmp/bearmulti2.yuv");
#endif
          ReturnFrame(video.get(), frame);
          if (frame_count1 == 30)
            wait_valid1.set_value();
        });

    // The default stack size is ZIRCON_DEFAULT_STACK_SIZE - 256kB.
    uint8_t padding[16384] = {};
    {
      StreamBuffer* buffer = video->decoder_instances_.front().stream_buffer();
      auto test_ivf =
          TestSupport::LoadFirmwareFile("video_test_data/test-25fps.vp9");
      ASSERT_NE(nullptr, test_ivf);
      auto aml_data = ConvertIvfToAmlVFrames(test_ivf->ptr, test_ivf->size);
      video->core_->InitializeDirectInput();
      // Only use the first 50 frames to save time.
      for (uint32_t i = 0; i < 50; i++) {
        EXPECT_EQ(ZX_OK, video->ProcessVideoNoParser(aml_data[i].data.data(),
                                                     aml_data[i].data.size()));
      }
      buffer->set_padding_size(sizeof(padding));
      // Force all frames to be processed.
      EXPECT_EQ(ZX_OK, video->ProcessVideoNoParser(padding, sizeof(padding)));
    }

    // Normally we'd probably want to always fill the stream buffer when the
    // decoder is attached to the hardware, but for testing we should try
    // filling the buffer when it's not attached, to ensure we can correctly
    // initialize the write pointer later.
    {
      auto test_ivf2 =
          TestSupport::LoadFirmwareFile("video_test_data/test-25fps.vp9_2");
      ASSERT_NE(nullptr, test_ivf2);
      auto aml_data2 = ConvertIvfToAmlVFrames(test_ivf2->ptr, test_ivf2->size);
      StreamBuffer* buffer = video->decoder_instances_.back().stream_buffer();
      uint32_t offset = 0;
      // Only use the first 30 frames to save time. Ensure this is different
      // from above, to test whether ending decoding early works.
      for (uint32_t i = 0; i < 30; i++) {
        memcpy((uint8_t*)io_buffer_virt(buffer->buffer()) + offset,
               aml_data2[i].data.data(), aml_data2[i].data.size());
        offset += aml_data2[i].data.size();
      }
      buffer->set_data_size(offset);
      buffer->set_padding_size(sizeof(padding));
      memcpy((uint8_t*)io_buffer_virt(buffer->buffer()) + offset, padding,
             sizeof(padding));
      offset += sizeof(padding);
      io_buffer_cache_flush(buffer->buffer(), 0, offset);
    }
    video->core()->StartDecoding();

    EXPECT_EQ(std::future_status::ready,
              wait_valid.get_future().wait_for(std::chrono::seconds(10)));

    EXPECT_EQ(std::future_status::ready,
              wait_valid1.get_future().wait_for(std::chrono::seconds(10)));

    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->decoder_instances_.clear();
      video->video_decoder_ = nullptr;
    }
    video.reset();
  }

 private:
  // This is called from the interrupt handler, which already holds the lock.
  static void ReturnFrame(AmlogicVideo* video,
                          std::shared_ptr<VideoFrame> frame)
      FXL_NO_THREAD_SAFETY_ANALYSIS {
    video->video_decoder_->ReturnFrame(frame);
  }
};

TEST(VP9, Decode) {
  TestVP9::Decode(true, "video_test_data/test-25fps.vp9", "/tmp/bearvp9.yuv");
}

TEST(VP9, DecodeNoParser) {
  TestVP9::Decode(false, "video_test_data/test-25fps.vp9",
                  "/tmp/bearvp9noparser.yuv");
}

TEST(VP9, Decode10Bit) {
  TestVP9::Decode(false, "video_test_data/test-25fps.vp9_2",
                  "/tmp/bearvp9noparser.yuv");
}

TEST(VP9, DecodePerFrame) { TestVP9::DecodePerFrame(); }

TEST(VP9, DecodeResetHardware) {
  TestVP9::DecodeResetHardware("/tmp/bearvp9reset.yuv");
}

TEST(VP9, DecodeMultiInstance) { TestVP9::DecodeMultiInstance(); }
