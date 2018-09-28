// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/mixer/linear_sampler.h"

#include <algorithm>
#include <limits>

#include "garnet/bin/media/audio_core/mixer/constants.h"
#include "garnet/bin/media/audio_core/mixer/mixer_utils.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {
namespace mixer {

// We specify alpha in fixed-point 19.13: a max val of "1.0" is 0x00002000.
constexpr float kFramesPerPtsSubframe = 1.0f / (1 << kPtsFractionalBits);

inline float Interpolate(float A, float B, uint32_t alpha) {
  return ((B - A) * kFramesPerPtsSubframe * alpha) + A;
}

template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
class LinearSamplerImpl : public LinearSampler {
 public:
  LinearSamplerImpl() : LinearSampler(FRAC_ONE - 1, FRAC_ONE - 1) { Reset(); }

  bool Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset,
           const void* src, uint32_t frac_src_frames, int32_t* frac_src_offset,
           uint32_t frac_step_size, Gain::AScale amplitude_scale,
           bool accumulate, uint32_t rate_modulo = 0, uint32_t denominator = 1,
           uint32_t* src_pos_modulo = nullptr) override;

  void Reset() override { ::memset(filter_data_, 0, sizeof(filter_data_)); }

 private:
  template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
  inline bool Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset,
                  const void* src, uint32_t frac_src_frames,
                  int32_t* frac_src_offset, uint32_t* src_pos_modulo,
                  uint32_t frac_step_size, uint32_t rate_modulo,
                  uint32_t denominator, Gain::AScale amplitude_scale);

  float filter_data_[2 * DestChanCount];
};

// TODO(mpuryear): MTWN-75 factor to minimize LinearSamplerImpl code duplication
template <typename SrcSampleType>
class NxNLinearSamplerImpl : public LinearSampler {
 public:
  NxNLinearSamplerImpl(size_t channelCount)
      : LinearSampler(FRAC_ONE - 1, FRAC_ONE - 1), chan_count_(channelCount) {
    filter_data_u_ = std::make_unique<float[]>(2 * chan_count_);
    Reset();
  }

  bool Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset,
           const void* src, uint32_t frac_src_frames, int32_t* frac_src_offset,
           uint32_t frac_step_size, Gain::AScale amplitude_scale,
           bool accumulate, uint32_t rate_modulo = 0, uint32_t denominator = 1,
           uint32_t* src_pos_modulo = nullptr) override;

  void Reset() override {
    ::memset(filter_data_u_.get(), 0,
             2 * chan_count_ * sizeof(filter_data_u_[0]));
  }

 private:
  template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
  inline bool Mix(float* dest, uint32_t dest_frames, uint32_t* dest_offset,
                  const void* src, uint32_t frac_src_frames,
                  int32_t* frac_src_offset, uint32_t* src_pos_modulo,
                  uint32_t frac_step_size, uint32_t rate_modulo,
                  uint32_t denominator, Gain::AScale amplitude_scale,
                  size_t chan_count);

  size_t chan_count_;
  std::unique_ptr<float[]> filter_data_u_;
};

// If upper layers call with ScaleType MUTED, they must set DoAccumulate=TRUE.
// They guarantee new buffers are cleared before usage; we optimize accordingly.
template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
inline bool LinearSamplerImpl<DestChanCount, SrcSampleType, SrcChanCount>::Mix(
    float* dest, uint32_t dest_frames, uint32_t* dest_offset,
    const void* src_void, uint32_t frac_src_frames, int32_t* frac_src_offset,
    uint32_t* src_pos_modulo, uint32_t frac_step_size, uint32_t rate_modulo,
    uint32_t denominator, Gain::AScale amplitude_scale) {
  static_assert(
      ScaleType != ScalerType::MUTED || DoAccumulate == true,
      "Mixing muted streams without accumulation is explicitly unsupported");

  // If caller didn't specify a running position modulo param, use one locally.
  uint32_t temp_modulo = 0;
  if (src_pos_modulo == nullptr) {
    src_pos_modulo = &temp_modulo;
  }

  // Although the number of source frames is expressed in fixed-point 19.13
  // format, the actual number of frames must always be an integer.
  FXL_DCHECK((frac_src_frames & kPtsFractionalMask) == 0);
  FXL_DCHECK(frac_src_frames >= FRAC_ONE);
  // Interpolation offset is int32, so even though frac_src_frames is a uint32,
  // callers should not exceed int32_t::max().
  FXL_DCHECK(frac_src_frames <=
             static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));

  using SR = SrcReader<SrcSampleType, SrcChanCount, DestChanCount>;
  using DM = DestMixer<ScaleType, DoAccumulate>;
  const SrcSampleType* src = static_cast<const SrcSampleType*>(src_void);
  uint32_t dest_off = *dest_offset;
  int32_t src_off = *frac_src_offset;

  FXL_DCHECK(denominator > 0);
  FXL_DCHECK(denominator > rate_modulo);

  // "Source end" is the last valid input sub-frame that can be sampled.
  int32_t src_end =
      static_cast<int32_t>(frac_src_frames - pos_filter_width() - 1);

  FXL_DCHECK(dest_off < dest_frames);
  FXL_DCHECK(src_end >= 0);
  FXL_DCHECK(frac_src_frames <=
             static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));
  // "Source offset" can be negative, but within the bounds of pos_filter_width.
  // Otherwise, all these samples are in the future and irrelevant here. Callers
  // explicitly avoid calling Mix in this case, so we have detected an error.
  // For linear_sampler it implies a requirement that src_off > -FRAC_ONE.
  FXL_DCHECK(src_off + static_cast<int32_t>(pos_filter_width()) >= 0);
  // Source offset must also be within neg_filter_width of our last sample.
  // Otherwise, all these samples are in the past and irrelevant here. Callers
  // explicitly avoid calling Mix in this case, so we have detected an error.
  // For linear_sampler this implies that src_off < frac_src_frames.
  FXL_DCHECK(src_off + FRAC_ONE <= frac_src_frames + neg_filter_width());

  // If we are not attenuated to the point of being muted, go ahead and perform
  // the mix.  Otherwise, just update the source and dest offsets and hold onto
  // any relevant filter data from the end of the source.
  if (ScaleType != ScalerType::MUTED) {
    // If src_off is negative, we must incorporate previously-cached samples.
    // Add a new sample, to complete the filter set, and compute the output.
    if (src_off < 0) {
      for (size_t D = 0; D < DestChanCount; ++D) {
        filter_data_[DestChanCount + D] = SR::Read(src + (D / SR::DestPerSrc));
      }

      do {
        float* out = dest + (dest_off * DestChanCount);

        for (size_t D = 0; D < DestChanCount; ++D) {
          float sample =
              Interpolate(filter_data_[D], filter_data_[DestChanCount + D],
                          src_off + FRAC_ONE);
          out[D] = DM::Mix(out[D], sample, amplitude_scale);
        }

        dest_off += 1;
        src_off += frac_step_size;

        if (HasModulo) {
          *src_pos_modulo = (*src_pos_modulo) + rate_modulo;
          if (*src_pos_modulo >= denominator) {
            ++src_off;
            *src_pos_modulo = (*src_pos_modulo) - denominator;
          }
        }
      } while ((dest_off < dest_frames) && (src_off < 0));
    }

    // Now we are fully in the current buffer and need not rely on our cache.
    while ((dest_off < dest_frames) && (src_off < src_end)) {
      uint32_t S = (src_off >> kPtsFractionalBits) * SrcChanCount;
      float* out = dest + (dest_off * DestChanCount);

      for (size_t D = 0; D < DestChanCount; ++D) {
        float s1 = SR::Read(src + S + (D / SR::DestPerSrc));
        float s2 = SR::Read(src + S + (D / SR::DestPerSrc) + SrcChanCount);
        float sample = Interpolate(s1, s2, src_off & FRAC_MASK);
        out[D] = DM::Mix(out[D], sample, amplitude_scale);
      }

      dest_off += 1;
      src_off += frac_step_size;

      if (HasModulo) {
        *src_pos_modulo = (*src_pos_modulo) + rate_modulo;
        if (*src_pos_modulo >= denominator) {
          ++src_off;
          *src_pos_modulo = (*src_pos_modulo) - denominator;
        }
      }
    }
  } else {
    // We are muted. Don't mix, but figure out how many samples we WOULD have
    // produced and update the src_off and dest_off values appropriately.
    if ((dest_off < dest_frames) && (src_off < src_end)) {
      uint32_t src_avail =
          (((src_end - src_off) + frac_step_size - 1) / frac_step_size);
      uint32_t dest_avail = (dest_frames - dest_off);
      uint32_t avail = std::min(src_avail, dest_avail);

      dest_off += avail;
      src_off += avail * frac_step_size;

      if (HasModulo) {
        *src_pos_modulo = (*src_pos_modulo) + (rate_modulo * avail);
        src_off += (*src_pos_modulo / denominator);
        *src_pos_modulo = (*src_pos_modulo) % denominator;
      }
    }
  }

  // If we have room for at least one more sample, and our sampling position
  // hits the input buffer's final frame exactly ...
  if ((dest_off < dest_frames) && (src_off == src_end)) {
    // ... and if we are not muted, of course ...
    if (ScaleType != ScalerType::MUTED) {
      // ... then we can _point-sample_ one final frame into our output buffer.
      // We need not _interpolate_ since fractional position is exactly zero.
      uint32_t S = (src_off >> kPtsFractionalBits) * SrcChanCount;
      float* out = dest + (dest_off * DestChanCount);

      for (size_t D = 0; D < DestChanCount; ++D) {
        float sample = SR::Read(src + S + (D / SR::DestPerSrc));
        out[D] = DM::Mix(out[D], sample, amplitude_scale);
      }
    }

    dest_off += 1;
    src_off += frac_step_size;

    if (HasModulo) {
      *src_pos_modulo = (*src_pos_modulo) + rate_modulo;
      if (*src_pos_modulo >= denominator) {
        ++src_off;
        *src_pos_modulo = (*src_pos_modulo) - denominator;
      }
    }
  }

  *dest_offset = dest_off;
  *frac_src_offset = src_off;

  // If next source position to consume is beyond start of last frame ...
  if (src_off > src_end) {
    uint32_t S = (src_end >> kPtsFractionalBits) * SrcChanCount;
    // ... and if we are not mute, of course...
    if (ScaleType != ScalerType::MUTED) {
      // ... cache our final frame for use in future interpolation ...
      for (size_t D = 0; D < DestChanCount; ++D) {
        filter_data_[D] = SR::Read(src + S + (D / SR::DestPerSrc));
      }
    } else {
      // ... otherwise cache silence (which is what we actually produced).
      for (size_t D = 0; D < DestChanCount; ++D) {
        filter_data_[D] = 0;
      }
    }
    // At this point the source offset (src_off) is either somewhere within
    // the last source sample, or entirely beyond the end of the source buffer
    // (if frac_step_size is greater than unity).  Either way, we've extracted
    // all of the information from this source buffer, and can return TRUE.
    return true;
  }

  // Source offset (src_off) is at or before the start of the last source
  // sample. We have not exhausted this source buffer -- return FALSE.
  return false;
}

template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
bool LinearSamplerImpl<DestChanCount, SrcSampleType, SrcChanCount>::Mix(
    float* dest, uint32_t dest_frames, uint32_t* dest_offset, const void* src,
    uint32_t frac_src_frames, int32_t* frac_src_offset, uint32_t frac_step_size,
    Gain::AScale amplitude_scale, bool accumulate, uint32_t rate_modulo,
    uint32_t denominator, uint32_t* src_pos_modulo) {
  if (amplitude_scale == Gain::kUnityScale) {
    return accumulate ? (rate_modulo > 0
                             ? Mix<ScalerType::EQ_UNITY, true, true>(
                                   dest, dest_frames, dest_offset, src,
                                   frac_src_frames, frac_src_offset,
                                   src_pos_modulo, frac_step_size, rate_modulo,
                                   denominator, amplitude_scale)
                             : Mix<ScalerType::EQ_UNITY, true, false>(
                                   dest, dest_frames, dest_offset, src,
                                   frac_src_frames, frac_src_offset,
                                   src_pos_modulo, frac_step_size, rate_modulo,
                                   denominator, amplitude_scale))
                      : (rate_modulo > 0
                             ? Mix<ScalerType::EQ_UNITY, false, true>(
                                   dest, dest_frames, dest_offset, src,
                                   frac_src_frames, frac_src_offset,
                                   src_pos_modulo, frac_step_size, rate_modulo,
                                   denominator, amplitude_scale)
                             : Mix<ScalerType::EQ_UNITY, false, false>(
                                   dest, dest_frames, dest_offset, src,
                                   frac_src_frames, frac_src_offset,
                                   src_pos_modulo, frac_step_size, rate_modulo,
                                   denominator, amplitude_scale));
  } else if (amplitude_scale <= Gain::kMinScale) {
    return (rate_modulo > 0
                ? Mix<ScalerType::MUTED, true, true>(
                      dest, dest_frames, dest_offset, src, frac_src_frames,
                      frac_src_offset, src_pos_modulo, frac_step_size,
                      rate_modulo, denominator, amplitude_scale)
                : Mix<ScalerType::MUTED, true, false>(
                      dest, dest_frames, dest_offset, src, frac_src_frames,
                      frac_src_offset, src_pos_modulo, frac_step_size,
                      rate_modulo, denominator, amplitude_scale));
  } else {
    return accumulate ? (rate_modulo > 0
                             ? Mix<ScalerType::NE_UNITY, true, true>(
                                   dest, dest_frames, dest_offset, src,
                                   frac_src_frames, frac_src_offset,
                                   src_pos_modulo, frac_step_size, rate_modulo,
                                   denominator, amplitude_scale)
                             : Mix<ScalerType::NE_UNITY, true, false>(
                                   dest, dest_frames, dest_offset, src,
                                   frac_src_frames, frac_src_offset,
                                   src_pos_modulo, frac_step_size, rate_modulo,
                                   denominator, amplitude_scale))
                      : (rate_modulo > 0
                             ? Mix<ScalerType::NE_UNITY, false, true>(
                                   dest, dest_frames, dest_offset, src,
                                   frac_src_frames, frac_src_offset,
                                   src_pos_modulo, frac_step_size, rate_modulo,
                                   denominator, amplitude_scale)
                             : Mix<ScalerType::NE_UNITY, false, false>(
                                   dest, dest_frames, dest_offset, src,
                                   frac_src_frames, frac_src_offset,
                                   src_pos_modulo, frac_step_size, rate_modulo,
                                   denominator, amplitude_scale));
  }
}

// If upper layers call with ScaleType MUTED, they must set DoAccumulate=TRUE.
// They guarantee new buffers are cleared before usage; we optimize accordingly.
template <typename SrcSampleType>
template <ScalerType ScaleType, bool DoAccumulate, bool HasModulo>
inline bool NxNLinearSamplerImpl<SrcSampleType>::Mix(
    float* dest, uint32_t dest_frames, uint32_t* dest_offset,
    const void* src_void, uint32_t frac_src_frames, int32_t* frac_src_offset,
    uint32_t* src_pos_modulo, uint32_t frac_step_size, uint32_t rate_modulo,
    uint32_t denominator, Gain::AScale amplitude_scale, size_t chan_count) {
  static_assert(
      ScaleType != ScalerType::MUTED || DoAccumulate == true,
      "Mixing muted streams without accumulation is explicitly unsupported");

  // If caller didn't specify a running position modulo param, use one locally.
  uint32_t temp_modulo = 0;
  if (src_pos_modulo == nullptr) {
    src_pos_modulo = &temp_modulo;
  }

  // Although the number of source frames is expressed in fixed-point 19.13
  // format, the actual number of frames must always be an integer.
  FXL_DCHECK((frac_src_frames & kPtsFractionalMask) == 0);
  FXL_DCHECK(frac_src_frames >= FRAC_ONE);
  // Interpolation offset is int32, so even though frac_src_frames is a uint32,
  // callers should not exceed int32_t::max().
  FXL_DCHECK(frac_src_frames <=
             static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));

  using DM = DestMixer<ScaleType, DoAccumulate>;
  const SrcSampleType* src = static_cast<const SrcSampleType*>(src_void);
  uint32_t dest_off = *dest_offset;
  int32_t src_off = *frac_src_offset;

  FXL_DCHECK(denominator > 0);
  FXL_DCHECK(denominator > rate_modulo);

  // This is the last sub-frame at which we can output without additional data.
  int32_t src_end =
      static_cast<int32_t>(frac_src_frames - pos_filter_width() - 1);

  FXL_DCHECK(dest_off < dest_frames);
  FXL_DCHECK(src_end >= 0);
  FXL_DCHECK(frac_src_frames <=
             static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));

  // "Source offset" can be negative, but within the bounds of pos_filter_width.
  // For linear_sampler this means that src_off > -FRAC_ONE.
  FXL_DCHECK(src_off + static_cast<int32_t>(pos_filter_width()) >= 0);
  // Source offset must also be within neg_filter_width of our last sample.
  FXL_DCHECK(src_off + FRAC_ONE <= frac_src_frames + neg_filter_width());

  // If we are not attenuated to the point of being muted, go ahead and perform
  // the mix.  Otherwise, just update the source and dest offsets and hold onto
  // any relevant filter data from the end of the source.
  if (ScaleType != ScalerType::MUTED) {
    // When starting "between buffers", we must rely on previously-cached vals.
    if (src_off < 0) {
      for (size_t D = 0; D < chan_count; ++D) {
        filter_data_u_[chan_count + D] =
            SampleNormalizer<SrcSampleType>::Read(src + D);
      }

      do {
        float* out = dest + (dest_off * chan_count);

        for (size_t D = 0; D < chan_count; ++D) {
          float sample = Interpolate(filter_data_u_[chan_count + D],
                                     filter_data_u_[D], -src_off);
          out[D] = DM::Mix(out[D], sample, amplitude_scale);
        }

        dest_off += 1;
        src_off += frac_step_size;

        if (HasModulo) {
          *src_pos_modulo = (*src_pos_modulo) + rate_modulo;
          if (*src_pos_modulo >= denominator) {
            ++src_off;
            *src_pos_modulo = (*src_pos_modulo) - denominator;
          }
        }
      } while ((dest_off < dest_frames) && (src_off < 0));
    }

    // Now we are fully in the current buffer and need not rely on our cache.
    while ((dest_off < dest_frames) && (src_off < src_end)) {
      uint32_t S = (src_off >> kPtsFractionalBits) * chan_count;
      float* out = dest + (dest_off * chan_count);

      for (size_t D = 0; D < chan_count; ++D) {
        float s1 = SampleNormalizer<SrcSampleType>::Read(src + S + D);
        float s2 =
            SampleNormalizer<SrcSampleType>::Read(src + S + D + chan_count);
        float sample = Interpolate(s1, s2, src_off & FRAC_MASK);
        out[D] = DM::Mix(out[D], sample, amplitude_scale);
      }

      dest_off += 1;
      src_off += frac_step_size;

      if (HasModulo) {
        *src_pos_modulo = (*src_pos_modulo) + rate_modulo;
        if (*src_pos_modulo >= denominator) {
          ++src_off;
          *src_pos_modulo = (*src_pos_modulo) - denominator;
        }
      }
    }
  } else {
    // We are muted. Don't mix, but figure out how many samples we WOULD have
    // produced and update the src_off and dest_off values appropriately.
    if ((dest_off < dest_frames) && (src_off < src_end)) {
      uint32_t src_avail =
          (((src_end - src_off) + frac_step_size - 1) / frac_step_size);
      uint32_t dest_avail = (dest_frames - dest_off);
      uint32_t avail = std::min(src_avail, dest_avail);

      dest_off += avail;
      src_off += avail * frac_step_size;

      if (HasModulo) {
        *src_pos_modulo = (*src_pos_modulo) + (rate_modulo * avail);
        src_off += (*src_pos_modulo / denominator);
        *src_pos_modulo = (*src_pos_modulo) % denominator;
      }
    }
  }

  // If we have room for at least one more sample, and our sampling position
  // hits the input buffer's final frame exactly ...
  if ((dest_off < dest_frames) && (src_off == src_end)) {
    // ... and if we are not muted, of course ...
    if (ScaleType != ScalerType::MUTED) {
      // ... then we can _point-sample_ one final frame into our output buffer.
      // We need not _interpolate_ since fractional position is exactly zero.
      uint32_t S = (src_off >> kPtsFractionalBits) * chan_count;
      float* out = dest + (dest_off * chan_count);

      for (size_t D = 0; D < chan_count; ++D) {
        float sample = SampleNormalizer<SrcSampleType>::Read(src + S + D);
        out[D] = DM::Mix(out[D], sample, amplitude_scale);
      }
    }

    dest_off += 1;
    src_off += frac_step_size;

    if (HasModulo) {
      *src_pos_modulo = (*src_pos_modulo) + rate_modulo;
      if (*src_pos_modulo >= denominator) {
        ++src_off;
        *src_pos_modulo = (*src_pos_modulo) - denominator;
      }
    }
  }

  *dest_offset = dest_off;
  *frac_src_offset = src_off;

  // If next source position to consume is beyond start of last frame ...
  if (src_off > src_end) {
    uint32_t S = (src_end >> kPtsFractionalBits) * chan_count;
    // ... and if we are not mute, of course...
    if (ScaleType != ScalerType::MUTED) {
      // ... cache our final frame for use in future interpolation ...
      for (size_t D = 0; D < chan_count; ++D) {
        filter_data_u_[D] = SampleNormalizer<SrcSampleType>::Read(src + S + D);
      }
    } else {
      // ... otherwise cache silence (which is what we actually produced).
      for (size_t D = 0; D < chan_count; ++D) {
        filter_data_u_[D] = 0;
      }
    }
    // At this point the source offset (src_off) is either somewhere within
    // the last source sample, or entirely beyond the end of the source buffer
    // (if frac_step_size is greater than unity).  Either way, we've extracted
    // all of the information from this source buffer, and can return TRUE.
    return true;
  }

  // Source offset (src_off) is at or before the start of the last source
  // sample. We have not exhausted this source buffer -- return FALSE.
  return false;
}

template <typename SrcSampleType>
bool NxNLinearSamplerImpl<SrcSampleType>::Mix(
    float* dest, uint32_t dest_frames, uint32_t* dest_offset, const void* src,
    uint32_t frac_src_frames, int32_t* frac_src_offset, uint32_t frac_step_size,
    Gain::AScale amplitude_scale, bool accumulate, uint32_t rate_modulo,
    uint32_t denominator, uint32_t* src_pos_modulo) {
  if (amplitude_scale == Gain::kUnityScale) {
    return accumulate ? (rate_modulo > 0
                             ? Mix<ScalerType::EQ_UNITY, true, true>(
                                   dest, dest_frames, dest_offset, src,
                                   frac_src_frames, frac_src_offset,
                                   src_pos_modulo, frac_step_size, rate_modulo,
                                   denominator, amplitude_scale, chan_count_)
                             : Mix<ScalerType::EQ_UNITY, true, false>(
                                   dest, dest_frames, dest_offset, src,
                                   frac_src_frames, frac_src_offset,
                                   src_pos_modulo, frac_step_size, rate_modulo,
                                   denominator, amplitude_scale, chan_count_))
                      : (rate_modulo > 0
                             ? Mix<ScalerType::EQ_UNITY, false, true>(
                                   dest, dest_frames, dest_offset, src,
                                   frac_src_frames, frac_src_offset,
                                   src_pos_modulo, frac_step_size, rate_modulo,
                                   denominator, amplitude_scale, chan_count_)
                             : Mix<ScalerType::EQ_UNITY, false, false>(
                                   dest, dest_frames, dest_offset, src,
                                   frac_src_frames, frac_src_offset,
                                   src_pos_modulo, frac_step_size, rate_modulo,
                                   denominator, amplitude_scale, chan_count_));
  } else if (amplitude_scale <= Gain::kMinScale) {
    return (rate_modulo > 0
                ? Mix<ScalerType::MUTED, true, true>(
                      dest, dest_frames, dest_offset, src, frac_src_frames,
                      frac_src_offset, src_pos_modulo, frac_step_size,
                      rate_modulo, denominator, amplitude_scale, chan_count_)
                : Mix<ScalerType::MUTED, true, false>(
                      dest, dest_frames, dest_offset, src, frac_src_frames,
                      frac_src_offset, src_pos_modulo, frac_step_size,
                      rate_modulo, denominator, amplitude_scale, chan_count_));
  } else {
    return accumulate ? (rate_modulo > 0
                             ? Mix<ScalerType::NE_UNITY, true, true>(
                                   dest, dest_frames, dest_offset, src,
                                   frac_src_frames, frac_src_offset,
                                   src_pos_modulo, frac_step_size, rate_modulo,
                                   denominator, amplitude_scale, chan_count_)
                             : Mix<ScalerType::NE_UNITY, true, false>(
                                   dest, dest_frames, dest_offset, src,
                                   frac_src_frames, frac_src_offset,
                                   src_pos_modulo, frac_step_size, rate_modulo,
                                   denominator, amplitude_scale, chan_count_))
                      : (rate_modulo > 0
                             ? Mix<ScalerType::NE_UNITY, false, true>(
                                   dest, dest_frames, dest_offset, src,
                                   frac_src_frames, frac_src_offset,
                                   src_pos_modulo, frac_step_size, rate_modulo,
                                   denominator, amplitude_scale, chan_count_)
                             : Mix<ScalerType::NE_UNITY, false, false>(
                                   dest, dest_frames, dest_offset, src,
                                   frac_src_frames, frac_src_offset,
                                   src_pos_modulo, frac_step_size, rate_modulo,
                                   denominator, amplitude_scale, chan_count_));
  }
}

// Templates used to expand all of the different combinations of the possible
// LinearSampler Mixer configurations.
template <size_t DestChanCount, typename SrcSampleType, size_t SrcChanCount>
static inline MixerPtr SelectLSM(
    const fuchsia::media::AudioStreamType& src_format,
    const fuchsia::media::AudioStreamType& dest_format) {
  return MixerPtr(
      new LinearSamplerImpl<DestChanCount, SrcSampleType, SrcChanCount>());
}

template <size_t DestChanCount, typename SrcSampleType>
static inline MixerPtr SelectLSM(
    const fuchsia::media::AudioStreamType& src_format,
    const fuchsia::media::AudioStreamType& dest_format) {
  switch (src_format.channels) {
    case 1:
      return SelectLSM<DestChanCount, SrcSampleType, 1>(src_format,
                                                        dest_format);
    case 2:
      return SelectLSM<DestChanCount, SrcSampleType, 2>(src_format,
                                                        dest_format);
    default:
      return nullptr;
  }
}

template <size_t DestChanCount>
static inline MixerPtr SelectLSM(
    const fuchsia::media::AudioStreamType& src_format,
    const fuchsia::media::AudioStreamType& dest_format) {
  switch (src_format.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return SelectLSM<DestChanCount, uint8_t>(src_format, dest_format);
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return SelectLSM<DestChanCount, int16_t>(src_format, dest_format);
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      return SelectLSM<DestChanCount, int32_t>(src_format, dest_format);
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return SelectLSM<DestChanCount, float>(src_format, dest_format);
    default:
      return nullptr;
  }
}

static inline MixerPtr SelectNxNLSM(
    const fuchsia::media::AudioStreamType& src_format) {
  switch (src_format.sample_format) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return MixerPtr(new NxNLinearSamplerImpl<uint8_t>(src_format.channels));
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return MixerPtr(new NxNLinearSamplerImpl<int16_t>(src_format.channels));
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      return MixerPtr(new NxNLinearSamplerImpl<int32_t>(src_format.channels));
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return MixerPtr(new NxNLinearSamplerImpl<float>(src_format.channels));
    default:
      return nullptr;
  }
}

MixerPtr LinearSampler::Select(
    const fuchsia::media::AudioStreamType& src_format,
    const fuchsia::media::AudioStreamType& dest_format) {
  if (src_format.channels == dest_format.channels && src_format.channels > 2) {
    return SelectNxNLSM(src_format);
  }

  switch (dest_format.channels) {
    case 1:
      return SelectLSM<1>(src_format, dest_format);
    case 2:
      return SelectLSM<2>(src_format, dest_format);
    default:
      return nullptr;
  }
}

}  // namespace mixer
}  // namespace audio
}  // namespace media
