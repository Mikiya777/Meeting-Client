#include "render/src/video/UserContext.h"

#include "render/src/video/VideoRender.h"

#include <utility>

namespace RENDER {
UserContext::UserContext(std::string user_name, bool is_local)
    : m_user_name(std::move(user_name)), m_is_local(is_local) {
}

UserContext::~UserContext() {
  Uninitialize();
}

/*
UserContext 是帧来源管理器和选帧器。
VideoRender 是单用户的视频绘制器。

UserContext::Initialize() 会为这个用户创建 VideoRender，
并把该用户自己的 VideoShader 传给 VideoRender。
VideoRender::Initialize() 再初始化它内部的 YUVTexture、VideoMesh、VideoShader。
*/
bool UserContext::Initialize(std::shared_ptr<VideoShader> shader, int video_width, int video_height) {
  m_video_render = std::make_shared<VideoRender>(m_user_name, std::move(shader));
  return m_video_render->Initialize(video_width, video_height);
}

void UserContext::Uninitialize() {
  if (m_video_render) {
    m_video_render->Uninitialize();
    m_video_render.reset();
  }

  std::lock_guard<std::mutex> lock(m_frame_buffer_mutex);
  m_pending_frame.reset();
  m_last_frame.reset();
}

// 同一个绘制周期内优先保留时间戳更新的帧。
void UserContext::PushFrame(const std::shared_ptr<I420Frame> &frame) {
  if (!frame) {
    return;
  }

  std::lock_guard<std::mutex> lock(m_frame_buffer_mutex);
  if (!m_pending_frame || frame->timestamp_us >= m_pending_frame->timestamp_us) {
    m_pending_frame = frame;
  }
}

// UI/RenderEngine 触发渲染调用函数
bool UserContext::RenderFrame() {
  if (!m_video_render) {
    return false;
  }

  auto frame = PopFrameForRender();
  return frame ? m_video_render->RenderFrame(frame) : false;
}

//UI 窗口大小变化时，更新 OpenGL viewport
bool UserContext::UpdateVideoSize(int width, int height) {
  return m_video_render ? m_video_render->Resize(width, height) : false;
}

const std::string &UserContext::GetUserName() const {
  return m_user_name;
}

bool UserContext::IsLocalUser() const {
  return m_is_local;
}

std::shared_ptr<I420Frame> UserContext::PopFrameForRender() {
  std::lock_guard<std::mutex> lock(m_frame_buffer_mutex);
  if (m_pending_frame) {
    m_last_frame = std::move(m_pending_frame);
  }
  return m_last_frame;
}

} // namespace RENDER
