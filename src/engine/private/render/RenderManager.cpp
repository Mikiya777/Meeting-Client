#include "engine/private/render/RenderManager.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>

namespace VCE {
namespace {

constexpr int kDefaultSampleRate = 48000;
constexpr int kDefaultChannels = 2;
constexpr int kDefaultVideoWidth = 640;
constexpr int kDefaultVideoHeight = 480;

int64_t NowUs() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
}

uint8_t ClampByte(int value) {
  return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

} // namespace

RenderManager::RenderManager() = default;

RenderManager::~RenderManager() {
  Uninit();
}

Result RenderManager::Init() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_initialized) {
    return Result::kRet_AlreadyInitialized;
  }

  m_render_engine = RENDER::IRenderEngine::CreateRenderEngine();
  if (!m_render_engine ||
      !m_render_engine->Initialize(kDefaultSampleRate,
                                   kDefaultChannels,
                                   kDefaultVideoWidth,
                                   kDefaultVideoHeight)) {
    m_render_engine.reset();
    return Result::kRet_InternalError;
  }

  m_initialized = true;
  return Result::kRet_Success;
}

Result RenderManager::Uninit() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_Success;
  }

  if (m_render_engine) {
    m_render_engine->Uninitialize();
    m_render_engine.reset();
  }

  m_users.clear();
  m_pending_remote_video_frames.clear();
  m_initialized = false;
  return Result::kRet_Success;
}

Result RenderManager::AddUser(const std::string &user_id, bool is_local) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_render_engine) {
    return Result::kRet_NotInitialized;
  }
  if (user_id.empty()) {
    return Result::kRet_InvalidArgument;
  }

  if (!m_render_engine->AddUser(user_id, is_local)) {
    return Result::kRet_InternalError;
  }

  m_users.insert(user_id);

  VideoFrame first_frame;
  first_frame.user_id = user_id;
  first_frame.width = kDefaultVideoWidth;
  first_frame.height = kDefaultVideoHeight;
  first_frame.timestamp_us = NowUs();
  m_render_engine->PushVideoFrame(user_id, CreateRenderVideoFrame(first_frame, is_local));

  auto pending_it = m_pending_remote_video_frames.find(user_id);
  if (pending_it != m_pending_remote_video_frames.end()) {
    m_render_engine->PushVideoFrame(
        user_id, CreateRenderVideoFrame(pending_it->second, false));
    m_pending_remote_video_frames.erase(pending_it);
  }
  return Result::kRet_Success;
}

Result RenderManager::RemoveUser(const std::string &user_id) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_render_engine) {
    return Result::kRet_NotInitialized;
  }
  if (user_id.empty()) {
    return Result::kRet_InvalidArgument;
  }

  m_users.erase(user_id);
  m_pending_remote_video_frames.erase(user_id);
  return m_render_engine->RemoveUser(user_id) ? Result::kRet_Success
                                              : Result::kRet_InternalError;
}

Result RenderManager::RenderLocalVideoFrame(const VideoFrame &frame) {
  return PushVideoFrame(frame, true);
}

Result RenderManager::RenderRemoteVideoFrame(const VideoFrame &frame) {
  return PushVideoFrame(frame, false);
}

Result RenderManager::PlayRemoteAudioFrame(const AudioFrame &frame) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_render_engine) {
    return Result::kRet_NotInitialized;
  }
  if (frame.user_id.empty() || frame.sample_rate <= 0 ||
      frame.channels <= 0 || frame.samples <= 0) {
    return Result::kRet_InvalidArgument;
  }

  return m_render_engine->PushAudioFrame(frame.user_id, CreateRenderAudioFrame(frame))
             ? Result::kRet_Success
             : Result::kRet_InternalError;
}

Result RenderManager::RenderUser(const std::string &user_id) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_render_engine) {
    return Result::kRet_NotInitialized;
  }
  if (user_id.empty()) {
    return Result::kRet_InvalidArgument;
  }

  return m_render_engine->RenderUser(user_id) ? Result::kRet_Success
                                              : Result::kRet_InternalError;
}

Result RenderManager::UpdateUserVideoSize(const std::string &user_id,
                                          int width,
                                          int height) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_render_engine) {
    return Result::kRet_NotInitialized;
  }
  if (user_id.empty() || width <= 0 || height <= 0) {
    return Result::kRet_InvalidArgument;
  }

  return m_render_engine->UpdateUserVideoSize(user_id, width, height)
             ? Result::kRet_Success
             : Result::kRet_InternalError;
}

void RenderManager::SetPlaybackAudioCallback(PlaybackAudioCallback callback) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_render_engine) {
    m_render_engine->SetPlaybackAudioCallback(std::move(callback));
  }
}

Result RenderManager::PushVideoFrame(const VideoFrame &frame, bool is_local) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_render_engine) {
    return Result::kRet_NotInitialized;
  }
  if (frame.user_id.empty() || frame.width <= 0 || frame.height <= 0) {
    return Result::kRet_InvalidArgument;
  }

  if (m_users.find(frame.user_id) == m_users.end()) {
    if (!is_local && frame.HasData()) {
      m_pending_remote_video_frames[frame.user_id] = frame;
      return Result::kRet_Success;
    }
    return Result::kRet_NotInitialized;
  }

  return m_render_engine->PushVideoFrame(frame.user_id,
                                         CreateRenderVideoFrame(frame, is_local))
             ? Result::kRet_Success
             : Result::kRet_InternalError;
}

std::shared_ptr<RENDER::I420Frame> RenderManager::CreateRenderVideoFrame(
    const VideoFrame &frame,
    bool is_local) const {
  auto render_frame = std::make_shared<RENDER::I420Frame>();
  render_frame->width = frame.width;
  render_frame->height = frame.height;
  render_frame->timestamp_us = frame.timestamp_us == 0 ? NowUs() : frame.timestamp_us;

  if (frame.HasData()) {
    render_frame->data[0] = frame.data[0];
    render_frame->data[1] = frame.data[1];
    render_frame->data[2] = frame.data[2];
    return render_frame;
  }

  const size_t y_size = render_frame->GetYPlaneSize();
  const size_t uv_size = render_frame->GetUVPlaneSize();
  render_frame->data[0] = std::shared_ptr<uint8_t[]>(new uint8_t[y_size]);
  render_frame->data[1] = std::shared_ptr<uint8_t[]>(new uint8_t[uv_size]);
  render_frame->data[2] = std::shared_ptr<uint8_t[]>(new uint8_t[uv_size]);

  const auto hash_value = std::hash<std::string>{}(frame.user_id);
  const int base_y = is_local ? 112 : 88;
  const int base_u = 70 + static_cast<int>(hash_value % 90);
  const int base_v = 90 + static_cast<int>((hash_value / 7) % 90);

  for (int row = 0; row < frame.height; ++row) {
    for (int column = 0; column < frame.width; ++column) {
      const int wave = static_cast<int>(20.0 * std::sin((column + row) * 0.025));
      render_frame->data[0][static_cast<size_t>(row) * frame.width + column] =
          ClampByte(base_y + wave);
    }
  }

  std::fill_n(render_frame->data[1].get(), uv_size, ClampByte(base_u));
  std::fill_n(render_frame->data[2].get(), uv_size, ClampByte(base_v));
  return render_frame;
}

std::shared_ptr<RENDER::AudioFrame> RenderManager::CreateRenderAudioFrame(
    const AudioFrame &frame) const {
  auto render_frame = std::make_shared<RENDER::AudioFrame>();
  render_frame->sample_rate = frame.sample_rate;
  render_frame->channels = frame.channels;
  render_frame->samples_per_channel = frame.samples;
  render_frame->timestamp_us = frame.timestamp_us == 0 ? NowUs() : frame.timestamp_us;

  const size_t data_size = render_frame->DataSize();
  render_frame->data = std::shared_ptr<uint8_t[]>(new uint8_t[data_size]);
  if (frame.data && frame.DataSize() >= data_size) {
    std::memcpy(render_frame->data.get(), frame.data.get(), data_size);
  } else {
    std::fill_n(render_frame->data.get(), data_size, 0);
  }
  return render_frame;
}

} // namespace VCE
