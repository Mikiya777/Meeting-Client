#include "render/src/core/RenderEngine.h"

#include "render/src/audio/AudioMixer.h"
#include "render/src/audio/AudioRender.h"
#include "render/src/video/UserContext.h"

#include <utility>
#include "render/src/video/GLFrameWork/VideoShader.h"

namespace RENDER {

/*
1. 外部只依赖 IRenderEngine 抽象接口，不依赖 RenderEngine 具体类。
2. unique_ptr 表示调用方独占拥有这个 RenderEngine 对象，对象会自动释放。
*/
std::unique_ptr<IRenderEngine> IRenderEngine::CreateRenderEngine() {
  return std::make_unique<RenderEngine>();
}

// 计算 I420Frame 各平面的 CPU 数据大小，后续会用于纹理上传。
size_t I420Frame::GetYPlaneSize() const {
  return static_cast<size_t>(width) * static_cast<size_t>(height);
}

size_t I420Frame::GetUVPlaneSize() const {
  return GetYPlaneSize() / 4;
}

size_t I420Frame::GetDataSize() const {
  return GetYPlaneSize() + GetUVPlaneSize() * 2;
}

// 视频数据合法性判断
bool I420Frame::HasData() const {
  return width > 0 && height > 0 &&
         width % 2 == 0 && height % 2 == 0 &&
         data[0] && data[1] && data[2];
}

// 计算音频数据大小
size_t AudioFrame::DataSize() const {
  return static_cast<size_t>(samples_per_channel) * static_cast<size_t>(channels) * sizeof(float);
}

// 音频数据合法性判断
bool AudioFrame::HasData() const {
  return data != nullptr && sample_rate > 0 && channels > 0 && samples_per_channel > 0;
}

RenderEngine::RenderEngine() = default;

RenderEngine::~RenderEngine() {
  Uninitialize();
}

/*
初始化 RenderEngine 的公共资源
公共资源在 Initialize 创建
用户私有资源在 AddUser 创建
*/
bool RenderEngine::Initialize(int sample_rate, int channels, int video_width, int video_height) {
  if (m_initialized) {
    return true;
  }

  if (sample_rate <= 0 || channels <= 0 || video_width <= 0 || video_height <= 0 ||
      video_width % 2 != 0 || video_height % 2 != 0) {
    return false;
  }

  // 创建共享的混音器
  m_audio_mixer = std::make_shared<AudioMixer>();
  if (!m_audio_mixer->Initialize(sample_rate, channels)) {
    m_audio_mixer.reset();
    return false;
  }

  // RenderEngine独占AudioRender，申请后需要设置音频源
  m_audio_render = std::make_unique<AudioRender>();
  m_audio_render->SetAudioSource(m_audio_mixer);
  if (!m_audio_render->Initialize(sample_rate, channels)) {
    m_audio_render.reset();
    m_audio_mixer->Uninitialize();
    m_audio_mixer.reset();
    return false;
  }

  m_video_width = video_width;
  m_video_height = video_height;
  m_initialized = true;
  return true;
}

// 按依赖顺序反初始化：UserContext -> AudioRender -> AudioMixer。
void RenderEngine::Uninitialize() {
  if (!m_initialized) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(m_user_contexts_mutex);
    for (auto &[user_name, context] : m_user_contexts) {
      if (context) {
        context->Uninitialize();
      }
    }
    m_user_contexts.clear();
  }

  if (m_audio_render) {
    m_audio_render->Uninitialize();
    m_audio_render.reset();
  }

  if (m_audio_mixer) {
    m_audio_mixer->Uninitialize();
    m_audio_mixer.reset();
  }

  m_initialized = false;
}

bool RenderEngine::AddUser(const std::string &user_name, bool is_local) {
  if (!m_initialized || user_name.empty()) {
    return false;
  }

  // 需要对共享的user_contexts容器进行操作，需要保护。
  std::lock_guard<std::mutex> lock(m_user_contexts_mutex);
  if (m_user_contexts.find(user_name) != m_user_contexts.end()) {
    return true;
  }

  // 多个地方持有，让 UserContext 在一次 PushVideoFrame / RenderUser / UpdateUserVideoSize 调用过程中保持存活 （延长生命周期）
  // 创建该用户的视频上下文：负责帧缓存、选帧策略，并持有该用户的 VideoRender。
  auto context = std::make_shared<UserContext>(user_name, is_local);
  // Each QOpenGLWidget has its own OpenGL context, so each tile needs its own
  // shader program instead of sharing one GL object across contexts.
  auto shader = std::make_shared<VideoShader>();
  if (!context->Initialize(std::move(shader), m_video_width, m_video_height)) {
    return false;
  }

  m_user_contexts[user_name] = std::move(context);
  return true;
}

// 先检查，然后根据user_name找到具体的项后从容器中删除。
bool RenderEngine::RemoveUser(const std::string &user_name) {
  if (!m_initialized || user_name.empty()) {
    return false;
  }

  if (m_audio_mixer) {
    m_audio_mixer->RemoveUser(user_name);
  }

  std::lock_guard<std::mutex> lock(m_user_contexts_mutex);
  auto it = m_user_contexts.find(user_name);
  if (it != m_user_contexts.end()) {
    it->second->Uninitialize();
    m_user_contexts.erase(it);
  }
  return true;
}

// 如果用户的视频帧数据合法，则将它加入缓冲区中
bool RenderEngine::PushVideoFrame(const std::string &user_name, const std::shared_ptr<I420Frame> &frame) {
  auto context = GetUserContext(user_name);
  if (!context || !frame || !frame->HasData()) {
    return false;
  }
  context->PushFrame(frame);
  return true;
}

// 如果用户的音频帧数据合法，则将它加入混音器中
bool RenderEngine::PushAudioFrame(const std::string &user_name, const std::shared_ptr<AudioFrame> &frame) {
  if (!m_initialized || !m_audio_mixer || user_name.empty() || !frame || !frame->HasData()) {
    return false;
  }
  return m_audio_mixer->PushAudioFrame(user_name, frame);
}

// 使用指定用户的数据进行视频渲染，绘制
bool RenderEngine::RenderUser(const std::string &user_name) {
  auto context = GetUserContext(user_name);
  return context ? context->RenderFrame() : false;
}

// 更新某个用户视频窗口对应的 OpenGL viewport 尺寸。
bool RenderEngine::UpdateUserVideoSize(const std::string &user_name, int width, int height) {
  auto context = GetUserContext(user_name);
  return context ? context->UpdateVideoSize(width, height) : false;
}

//获取扬声器列表
std::vector<AudioSpeaker> RenderEngine::GetSpeakerDevices() {
  return m_audio_render ? m_audio_render->GetSpeakerDevices() : std::vector<AudioSpeaker>{};
}

// 更新选中的扬声器为当前扬声器
bool RenderEngine::UpdateAudioSpeaker(const std::string &device_id) {
  return m_audio_render ? m_audio_render->UpdateSpeaker(device_id) : false;
}

// 获取当前的扬声器id
bool RenderEngine::GetCurrentAudioSpeaker(std::string &device_id) {
  if (!m_audio_render) {
    return false;
  }
  device_id = m_audio_render->GetCurrentSpeakerId();
  return true;
}

void RenderEngine::SetPlaybackAudioCallback(PlaybackAudioCallback callback) {
  if (m_audio_render) {
    m_audio_render->SetPlaybackAudioCallback(std::move(callback));
  }
}

// 根据 user_name 获取对应用户的视频上下文。
std::shared_ptr<UserContext> RenderEngine::GetUserContext(const std::string &user_name) const {
  std::lock_guard<std::mutex> lock(m_user_contexts_mutex);
  auto it = m_user_contexts.find(user_name);
  return it == m_user_contexts.end() ? nullptr : it->second;
}

} // namespace RENDER
