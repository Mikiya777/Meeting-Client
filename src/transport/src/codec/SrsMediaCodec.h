#ifndef SRS_MEDIA_CODEC_H
#define SRS_MEDIA_CODEC_H

#include "transport/include/TransportDefine.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#if TRANSPORT_ENABLE_REAL_SRS
#include <opus.h>
#include <samplerate.h>

extern "C" {
#include <wels/codec_api.h>
}
#endif

/*
音视频编解码封装。
视频是 I420/YUV 与 H264 之间转换，音频是 PCM float 与 Opus 之间转换。
推流用编码器，拉流用解码器。
*/
namespace TRANSPORT {

class H264Encoder {
public:
  ~H264Encoder();

  bool Open(const PublishInfo &info);
  bool Encode(const std::shared_ptr<I420Frame> &frame,
              std::vector<uint8_t> *encoded);
  void RequestKeyframe();
  void Close();

private:
  void CloseLocked();

private:
  std::mutex m_mutex;
#if TRANSPORT_ENABLE_REAL_SRS
  ISVCEncoder *m_encoder = nullptr;
#endif
  int m_width = 0;
  int m_height = 0;
  int m_keyframe_interval_frames = 60;
  int m_encoded_frame_count = 0;
  bool m_open = false;
  bool m_force_keyframe = false;
};

class OpusFloatEncoder {
public:
  ~OpusFloatEncoder();

  bool Open(const PublishInfo &info);
  bool Encode(const std::shared_ptr<AudioFrame> &frame,
              std::vector<std::vector<uint8_t>> *encoded_frames);
  void Close();

private:
  void CloseLocked();

private:
  std::mutex m_mutex;
#if TRANSPORT_ENABLE_REAL_SRS
  OpusEncoder *m_encoder = nullptr;
  SRC_STATE *m_resampler = nullptr;
#endif
  int m_input_sample_rate = 48000;
  int m_channels = 2;
  bool m_need_resample = false;
  bool m_open = false;
  std::vector<float> m_sample_buffer;
  std::vector<float> m_input_buffer;
  std::vector<uint8_t> m_output_buffer;
};

class H264Decoder {
public:
  ~H264Decoder();

  bool Open();
  bool Decode(const uint8_t *data,
              size_t size,
              int64_t timestamp_us,
              std::shared_ptr<I420Frame> *frame);
  // Stays true after a decode error until a clean IDR is decoded.
  bool NeedsKeyframe() const;
  void Close();

private:
  void CloseLocked();

private:
  mutable std::mutex m_mutex;
#if TRANSPORT_ENABLE_REAL_SRS
  ISVCDecoder *m_decoder = nullptr;
#endif
  bool m_open = false;
  bool m_needs_keyframe = false;
};

class OpusFloatDecoder {
public:
  ~OpusFloatDecoder();

  bool Open(const PublishInfo &info);
  bool Decode(const uint8_t *data,
              size_t size,
              int64_t timestamp_us,
              std::shared_ptr<AudioFrame> *frame);
  void Close();

private:
  void CloseLocked();

private:
  std::mutex m_mutex;
#if TRANSPORT_ENABLE_REAL_SRS
  OpusDecoder *m_decoder = nullptr;
  SRC_STATE *m_resampler = nullptr;
#endif
  int m_channels = 2;
  int m_output_sample_rate = 48000;
  bool m_need_resample = false;
  bool m_open = false;
  std::vector<float> m_decode_buffer;
};

} // namespace TRANSPORT

#endif // SRS_MEDIA_CODEC_H
