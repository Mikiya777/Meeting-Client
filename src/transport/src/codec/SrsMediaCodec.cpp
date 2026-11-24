#include "transport/src/codec/SrsMediaCodec.h"

#include "transport/src/srs/SrsTransportCommon.h"

#include <algorithm>
#include <climits>
#include <cstring>

#if TRANSPORT_ENABLE_REAL_SRS
extern "C" {
#include <wels/codec_app_def.h>
#include <wels/codec_def.h>
}
#endif

namespace TRANSPORT {
namespace {

#if TRANSPORT_ENABLE_REAL_SRS
bool ContainsIdrNal(const uint8_t *data, size_t size) {
  // The RTP depacketizer emits Annex B. The last three bytes of either
  // start code are 00 00 01.
  for (size_t i = 3; i < size; ++i) {
    if (data[i - 3] == 0 && data[i - 2] == 0 && data[i - 1] == 1 &&
        (data[i] & 0x1f) == 5) {
      return true;
    }
  }
  return false;
}
#endif

} // namespace

H264Encoder::~H264Encoder() {
  Close();
}

bool H264Encoder::Open(const PublishInfo &info) {
#if TRANSPORT_ENABLE_REAL_SRS
  std::lock_guard<std::mutex> lock(m_mutex);
  CloseLocked();

  if (WelsCreateSVCEncoder(&m_encoder) != 0 || !m_encoder) {
    return false;
  }

  SEncParamExt param;
  std::memset(&param, 0, sizeof(param));
  m_encoder->GetDefaultParams(&param);
  param.iUsageType = CAMERA_VIDEO_REAL_TIME;
  param.iPicWidth = info.video_width;
  param.iPicHeight = info.video_height;
  param.fMaxFrameRate = static_cast<float>(info.video_fps);
  param.iTargetBitrate = std::clamp(info.video_width *
                                        info.video_height *
                                        info.video_fps / 10,
                                    350000,
                                    1200000);
  param.iMaxBitrate = param.iTargetBitrate * 3 / 2;
  param.iRCMode = RC_BITRATE_MODE;
  param.bEnableFrameSkip = true;
  param.iComplexityMode = LOW_COMPLEXITY;
  param.iSpatialLayerNum = 1;
  param.uiIntraPeriod = static_cast<unsigned int>(
      std::max(12, info.video_fps));
  param.eSpsPpsIdStrategy = CONSTANT_ID;
  param.sSpatialLayers[0].iVideoWidth = info.video_width;
  param.sSpatialLayers[0].iVideoHeight = info.video_height;
  param.sSpatialLayers[0].fFrameRate = static_cast<float>(info.video_fps);
  param.sSpatialLayers[0].iSpatialBitrate = param.iTargetBitrate;
  param.sSpatialLayers[0].iMaxSpatialBitrate = param.iMaxBitrate;
  param.sSpatialLayers[0].uiProfileIdc = PRO_BASELINE;
  param.sSpatialLayers[0].uiLevelIdc = LEVEL_UNKNOWN;

  if (m_encoder->InitializeExt(&param) != 0) {
    CloseLocked();
    return false;
  }

  int format = videoFormatI420;
  m_encoder->SetOption(ENCODER_OPTION_DATAFORMAT, &format);
  int idr_interval = std::max(12, info.video_fps);
  m_encoder->SetOption(ENCODER_OPTION_IDR_INTERVAL, &idr_interval);
  EParameterSetStrategy strategy = CONSTANT_ID;
  m_encoder->SetOption(ENCODER_OPTION_SPS_PPS_ID_STRATEGY, &strategy);

  m_width = info.video_width;
  m_height = info.video_height;
  m_keyframe_interval_frames = idr_interval;
  m_encoded_frame_count = 0;
  m_open = true;
  return true;
#else
  (void)info;
  return false;
#endif
}

bool H264Encoder::Encode(const std::shared_ptr<I420Frame> &frame,
                         std::vector<uint8_t> *encoded) {
#if TRANSPORT_ENABLE_REAL_SRS
  if (!frame || !frame->IsValid() || !encoded) {
    return false;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_open || !m_encoder) {
    return false;
  }
  if (frame->width != m_width || frame->height != m_height) {
    RealSrsDebug("H264 encode rejected frame size " +
                 std::to_string(frame->width) + "x" +
                 std::to_string(frame->height) + ", expected " +
                 std::to_string(m_width) + "x" +
                 std::to_string(m_height));
    return false;
  }

  const bool periodic_keyframe =
      m_encoded_frame_count == 0 ||
      (m_keyframe_interval_frames > 0 &&
       m_encoded_frame_count % m_keyframe_interval_frames == 0);
  if (m_force_keyframe || periodic_keyframe) {
    if (m_encoder->ForceIntraFrame(true) == 0) {
      m_force_keyframe = false;
    }
  }

  SSourcePicture picture;
  std::memset(&picture, 0, sizeof(picture));
  picture.iPicWidth = frame->width;
  picture.iPicHeight = frame->height;
  picture.iColorFormat = videoFormatI420;
  picture.iStride[0] = frame->width;
  picture.iStride[1] = frame->width / 2;
  picture.iStride[2] = frame->width / 2;
  picture.pData[0] = frame->data[0].get();
  picture.pData[1] = frame->data[1].get();
  picture.pData[2] = frame->data[2].get();

  SFrameBSInfo frame_info;
  std::memset(&frame_info, 0, sizeof(frame_info));
  if (m_encoder->EncodeFrame(&picture, &frame_info) != 0) {
    return false;
  }

  if (frame_info.eFrameType == videoFrameTypeSkip) {
    encoded->clear();
    ++m_encoded_frame_count;
    return true;
  }

  size_t total_size = 0;
  for (int layer_index = 0; layer_index < frame_info.iLayerNum;
       ++layer_index) {
    const SLayerBSInfo &layer = frame_info.sLayerInfo[layer_index];
    for (int nal_index = 0; nal_index < layer.iNalCount; ++nal_index) {
      total_size += static_cast<size_t>(layer.pNalLengthInByte[nal_index]);
    }
  }

  if (total_size == 0) {
    return false;
  }

  encoded->resize(total_size);
  uint8_t *dst = encoded->data();
  for (int layer_index = 0; layer_index < frame_info.iLayerNum;
       ++layer_index) {
    const SLayerBSInfo &layer = frame_info.sLayerInfo[layer_index];
    uint8_t *nal = layer.pBsBuf;
    for (int nal_index = 0; nal_index < layer.iNalCount; ++nal_index) {
      const size_t size =
          static_cast<size_t>(layer.pNalLengthInByte[nal_index]);
      std::memcpy(dst, nal, size);
      dst += size;
      nal += size;
    }
  }
  ++m_encoded_frame_count;
  return true;
#else
  (void)frame;
  (void)encoded;
  return false;
#endif
}

void H264Encoder::RequestKeyframe() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_force_keyframe = true;
}

void H264Encoder::Close() {
  std::lock_guard<std::mutex> lock(m_mutex);
  CloseLocked();
}

void H264Encoder::CloseLocked() {
#if TRANSPORT_ENABLE_REAL_SRS
  if (m_encoder) {
    m_encoder->Uninitialize();
    WelsDestroySVCEncoder(m_encoder);
    m_encoder = nullptr;
  }
#endif
  m_open = false;
  m_force_keyframe = false;
  m_encoded_frame_count = 0;
}

OpusFloatEncoder::~OpusFloatEncoder() {
  Close();
}

bool OpusFloatEncoder::Open(const PublishInfo &info) {
#if TRANSPORT_ENABLE_REAL_SRS
  std::lock_guard<std::mutex> lock(m_mutex);
  CloseLocked();

  if (info.audio_channels <= 0) {
    return false;
  }

  int error = OPUS_OK;
  m_encoder = opus_encoder_create(kOpusSampleRate,
                                  info.audio_channels,
                                  OPUS_APPLICATION_VOIP,
                                  &error);
  if (error != OPUS_OK || !m_encoder) {
    return false;
  }

  opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(info.audio_channels * 64000));
  opus_encoder_ctl(m_encoder, OPUS_SET_VBR(1));
  opus_encoder_ctl(m_encoder, OPUS_SET_VBR_CONSTRAINT(1));
  opus_encoder_ctl(m_encoder, OPUS_SET_INBAND_FEC(1));
  opus_encoder_ctl(m_encoder, OPUS_SET_PACKET_LOSS_PERC(10));
  opus_encoder_ctl(m_encoder, OPUS_SET_DTX(1));
  opus_encoder_ctl(m_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
  opus_encoder_ctl(m_encoder, OPUS_SET_COMPLEXITY(6));

  m_input_sample_rate = info.audio_sample_rate;
  m_channels = info.audio_channels;
  m_need_resample = info.audio_sample_rate != kOpusSampleRate;
  if (m_need_resample) {
    int src_error = 0;
    m_resampler = src_new(SRC_SINC_FASTEST, m_channels, &src_error);
    if (src_error != 0 || !m_resampler) {
      CloseLocked();
      return false;
    }
  }

  m_input_buffer.resize(static_cast<size_t>(kOpusFrameSamples) *
                        static_cast<size_t>(m_channels));
  m_output_buffer.resize(4000);
  m_open = true;
  return true;
#else
  (void)info;
  return false;
#endif
}

bool OpusFloatEncoder::Encode(
    const std::shared_ptr<AudioFrame> &frame,
    std::vector<std::vector<uint8_t>> *encoded_frames) {
#if TRANSPORT_ENABLE_REAL_SRS
  if (!frame || !frame->IsValid() || !encoded_frames) {
    return false;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_open || !m_encoder || frame->channels != m_channels) {
    return false;
  }

  const auto *input = reinterpret_cast<const float *>(frame->data.get());
  const size_t input_frames = static_cast<size_t>(frame->samples);
  std::vector<float> converted;
  const float *samples = input;
  size_t sample_frames = input_frames;

  if (m_need_resample) {
    const double ratio =
        static_cast<double>(kOpusSampleRate) /
        static_cast<double>(m_input_sample_rate);
    const auto output_frames =
        static_cast<size_t>(static_cast<double>(input_frames) * ratio + 1.0);
    converted.resize(output_frames * static_cast<size_t>(m_channels));

    SRC_DATA data;
    std::memset(&data, 0, sizeof(data));
    data.data_in = input;
    data.input_frames = static_cast<long>(input_frames);
    data.data_out = converted.data();
    data.output_frames = static_cast<long>(output_frames);
    data.src_ratio = ratio;
    data.end_of_input = 0;
    if (src_process(m_resampler, &data) != 0) {
      return false;
    }
    converted.resize(static_cast<size_t>(data.output_frames_gen) *
                     static_cast<size_t>(m_channels));
    samples = converted.data();
    sample_frames = static_cast<size_t>(data.output_frames_gen);
  }

  const size_t sample_count =
      sample_frames * static_cast<size_t>(m_channels);
  for (size_t index = 0; index < sample_count; ++index) {
    m_sample_buffer.push_back(
        std::max(-1.0f, std::min(1.0f, samples[index])));
  }

  const size_t required =
      static_cast<size_t>(kOpusFrameSamples) * static_cast<size_t>(m_channels);
  while (m_sample_buffer.size() >= required) {
    std::copy_n(m_sample_buffer.begin(), required, m_input_buffer.begin());
    m_sample_buffer.erase(m_sample_buffer.begin(),
                          m_sample_buffer.begin() + required);

    const int encoded = opus_encode_float(
        m_encoder,
        m_input_buffer.data(),
        kOpusFrameSamples,
        m_output_buffer.data(),
        static_cast<opus_int32>(m_output_buffer.size()));
    if (encoded < 0) {
      return false;
    }

    encoded_frames->emplace_back(m_output_buffer.begin(),
                                 m_output_buffer.begin() + encoded);
  }
  return true;
#else
  (void)frame;
  (void)encoded_frames;
  return false;
#endif
}

void OpusFloatEncoder::Close() {
  std::lock_guard<std::mutex> lock(m_mutex);
  CloseLocked();
}

void OpusFloatEncoder::CloseLocked() {
#if TRANSPORT_ENABLE_REAL_SRS
  if (m_resampler) {
    src_delete(m_resampler);
    m_resampler = nullptr;
  }
  if (m_encoder) {
    opus_encoder_destroy(m_encoder);
    m_encoder = nullptr;
  }
#endif
  m_sample_buffer.clear();
  m_input_buffer.clear();
  m_output_buffer.clear();
  m_open = false;
}

H264Decoder::~H264Decoder() {
  Close();
}

bool H264Decoder::Open() {
#if TRANSPORT_ENABLE_REAL_SRS
  std::lock_guard<std::mutex> lock(m_mutex);
  CloseLocked();

  if (WelsCreateDecoder(&m_decoder) != 0 || !m_decoder) {
    return false;
  }

  SDecodingParam param;
  std::memset(&param, 0, sizeof(param));
  param.uiTargetDqLayer = UCHAR_MAX;
  param.eEcActiveIdc = ERROR_CON_SLICE_COPY;
  param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT;
  if (m_decoder->Initialize(&param) != 0) {
    CloseLocked();
    return false;
  }

  m_open = true;
  return true;
#else
  return false;
#endif
}

bool H264Decoder::Decode(const uint8_t *data,
                         size_t size,
                         int64_t timestamp_us,
                         std::shared_ptr<I420Frame> *frame) {
#if TRANSPORT_ENABLE_REAL_SRS
  if (data == nullptr || size == 0 || size > INT_MAX || frame == nullptr) {
    return false;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_open || !m_decoder) {
    return false;
  }

  uint8_t *planes[3] = {nullptr, nullptr, nullptr};
  SBufferInfo buffer_info;
  std::memset(&buffer_info, 0, sizeof(buffer_info));
  const DECODING_STATE result = m_decoder->DecodeFrameNoDelay(
      data,
      static_cast<int>(size),
      planes,
      &buffer_info);
  if (result != dsErrorFree && result != dsFramePending) {
    // OpenH264 may report errors and still offer a concealed picture.
    m_needs_keyframe = true;
    return false;
  }
  if (result != dsErrorFree || buffer_info.iBufferStatus != 1) {
    return false;
  }

  const auto &buffer = buffer_info.UsrData.sSystemBuffer;
  const int width = buffer.iWidth;
  const int height = buffer.iHeight;
  const int y_stride = buffer.iStride[0];
  const int uv_stride = buffer.iStride[1];
  if (width <= 0 || height <= 0 || width % 2 != 0 || height % 2 != 0 ||
      y_stride < width || uv_stride < width / 2 ||
      !planes[0] || !planes[1] || !planes[2]) {
    m_needs_keyframe = true;
    return false;
  }
  if (m_needs_keyframe && !ContainsIdrNal(data, size)) {
    return false;
  }

  auto output = std::make_shared<I420Frame>();
  output->width = width;
  output->height = height;
  output->timestamp_us = timestamp_us;

  const size_t y_size = output->GetYPlaneSize();
  const size_t uv_size = output->GetUVPlaneSize();
  output->data[0] = std::shared_ptr<uint8_t[]>(new uint8_t[y_size]);
  output->data[1] = std::shared_ptr<uint8_t[]>(new uint8_t[uv_size]);
  output->data[2] = std::shared_ptr<uint8_t[]>(new uint8_t[uv_size]);

  for (int row = 0; row < height; ++row) {
    std::memcpy(output->data[0].get() + row * width,
                planes[0] + row * y_stride,
                static_cast<size_t>(width));
  }

  const int uv_width = width / 2;
  const int uv_height = height / 2;
  for (int row = 0; row < uv_height; ++row) {
    std::memcpy(output->data[1].get() + row * uv_width,
                planes[1] + row * uv_stride,
                static_cast<size_t>(uv_width));
    std::memcpy(output->data[2].get() + row * uv_width,
                planes[2] + row * uv_stride,
                static_cast<size_t>(uv_width));
  }

  *frame = std::move(output);
  m_needs_keyframe = false;
  return true;
#else
  (void)data;
  (void)size;
  (void)timestamp_us;
  (void)frame;
  return false;
#endif
}

bool H264Decoder::NeedsKeyframe() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_needs_keyframe;
}

void H264Decoder::Close() {
  std::lock_guard<std::mutex> lock(m_mutex);
  CloseLocked();
}

void H264Decoder::CloseLocked() {
#if TRANSPORT_ENABLE_REAL_SRS
  if (m_decoder) {
    m_decoder->Uninitialize();
    WelsDestroyDecoder(m_decoder);
    m_decoder = nullptr;
  }
#endif
  m_open = false;
  m_needs_keyframe = false;
}

OpusFloatDecoder::~OpusFloatDecoder() {
  Close();
}

bool OpusFloatDecoder::Open(const PublishInfo &info) {
#if TRANSPORT_ENABLE_REAL_SRS
  std::lock_guard<std::mutex> lock(m_mutex);
  CloseLocked();

  if (info.audio_channels <= 0 || info.audio_sample_rate <= 0) {
    return false;
  }

  int error = OPUS_OK;
  m_decoder = opus_decoder_create(kOpusSampleRate,
                                  info.audio_channels,
                                  &error);
  if (error != OPUS_OK || !m_decoder) {
    return false;
  }

  m_channels = info.audio_channels;
  m_output_sample_rate = info.audio_sample_rate;
  m_need_resample = m_output_sample_rate != kOpusSampleRate;
  if (m_need_resample) {
    int src_error = 0;
    m_resampler = src_new(SRC_SINC_FASTEST, m_channels, &src_error);
    if (src_error != 0 || !m_resampler) {
      CloseLocked();
      return false;
    }
  }

  m_decode_buffer.resize(static_cast<size_t>(kOpusSampleRate / 10) *
                         static_cast<size_t>(m_channels));
  m_open = true;
  return true;
#else
  (void)info;
  return false;
#endif
}

bool OpusFloatDecoder::Decode(const uint8_t *data,
                              size_t size,
                              int64_t timestamp_us,
                              std::shared_ptr<AudioFrame> *frame) {
#if TRANSPORT_ENABLE_REAL_SRS
  if (data == nullptr || size == 0 || frame == nullptr) {
    return false;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_open || !m_decoder) {
    return false;
  }

  const int decoded_frames = opus_decode_float(
      m_decoder,
      data,
      static_cast<opus_int32>(size),
      m_decode_buffer.data(),
      static_cast<int>(m_decode_buffer.size() /
                       static_cast<size_t>(m_channels)),
      0);
  if (decoded_frames <= 0) {
    return false;
  }

  const float *samples = m_decode_buffer.data();
  size_t sample_frames = static_cast<size_t>(decoded_frames);
  std::vector<float> converted;

  if (m_need_resample) {
    const double ratio =
        static_cast<double>(m_output_sample_rate) /
        static_cast<double>(kOpusSampleRate);
    const auto output_frames =
        static_cast<size_t>(static_cast<double>(sample_frames) * ratio + 1.0);
    converted.resize(output_frames * static_cast<size_t>(m_channels));

    SRC_DATA src_data;
    std::memset(&src_data, 0, sizeof(src_data));
    src_data.data_in = samples;
    src_data.input_frames = static_cast<long>(sample_frames);
    src_data.data_out = converted.data();
    src_data.output_frames = static_cast<long>(output_frames);
    src_data.src_ratio = ratio;
    src_data.end_of_input = 0;
    if (src_process(m_resampler, &src_data) != 0) {
      return false;
    }
    converted.resize(static_cast<size_t>(src_data.output_frames_gen) *
                     static_cast<size_t>(m_channels));
    samples = converted.data();
    sample_frames = static_cast<size_t>(src_data.output_frames_gen);
  }

  auto output = std::make_shared<AudioFrame>();
  output->sample_rate = m_output_sample_rate;
  output->channels = m_channels;
  output->samples = static_cast<int>(sample_frames);
  output->timestamp_us = timestamp_us;

  const size_t byte_size =
      sample_frames * static_cast<size_t>(m_channels) * sizeof(float);
  output->data = std::shared_ptr<uint8_t[]>(new uint8_t[byte_size]);
  std::memcpy(output->data.get(), samples, byte_size);

  *frame = std::move(output);
  return true;
#else
  (void)data;
  (void)size;
  (void)timestamp_us;
  (void)frame;
  return false;
#endif
}

void OpusFloatDecoder::Close() {
  std::lock_guard<std::mutex> lock(m_mutex);
  CloseLocked();
}

void OpusFloatDecoder::CloseLocked() {
#if TRANSPORT_ENABLE_REAL_SRS
  if (m_resampler) {
    src_delete(m_resampler);
    m_resampler = nullptr;
  }
  if (m_decoder) {
    opus_decoder_destroy(m_decoder);
    m_decoder = nullptr;
  }
#endif
  m_decode_buffer.clear();
  m_open = false;
}

} // namespace TRANSPORT
