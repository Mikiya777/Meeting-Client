#include "render/src/video/VideoRender.h"

#include "render/src/video/GLFrameWork/VideoMesh.h"
#include "render/src/video/GLFrameWork/VideoShader.h"
#include "render/src/video/GLFrameWork/YUVTexture.h"

#include "render/src/video/GLFrameWork/OpenGLFunctions.h"

#include <utility>

namespace RENDER {

VideoRender::VideoRender(std::string user_name, std::shared_ptr<VideoShader> shared_shader)
    : m_user_name(std::move(user_name)), m_shader(std::move(shared_shader)) {
}

VideoRender::~VideoRender() {
  Uninitialize();
}

//创建下游需要的三个对象：shader,texture,mesh
bool VideoRender::Initialize(int video_width, int video_height) {
  if (m_initialized) {
    return true;
  }

  if (!m_shader) {
    return false;
  }

  RenderResult shader_result = m_shader->Initialize();
  if (shader_result != RenderResult::kSuccess &&
      shader_result != RenderResult::kAlreadyInitialized) {
    return false;
  }

  m_yuv_texture = std::make_unique<YUVTexture>();
  m_video_mesh = std::make_unique<VideoMesh>();

  RenderResult texture_result = m_yuv_texture->Initialize(video_width, video_height);
  if (texture_result != RenderResult::kSuccess &&
      texture_result != RenderResult::kAlreadyInitialized) {
    return false;
  }

  if (!m_video_mesh->Initialize()) {
    return false;
  }

  m_initialized = true;
  return true;
}

void VideoRender::Uninitialize() {
  if (m_video_mesh) {
    m_video_mesh->Uninitialize();
    m_video_mesh.reset();
  }

  if (m_yuv_texture) {
    m_yuv_texture->Uninitialize();
    m_yuv_texture.reset();
  }

  m_initialized = false;
}

// 执行一帧 OpenGL 视频渲染：上传 Y/U/V 数据，绑定 shader 和纹理，绘制矩形。
// YUV -> RGB 转换和纹理采样会在 Draw 触发的 fragment shader 中完成。
bool VideoRender::RenderFrame(const std::shared_ptr<I420Frame> &frame) {
  auto *gl = CurrentOpenGLFunctions();
  if (!gl) {
    return false;
  }

  if (!m_initialized || !frame || !frame->HasData()) {
    return false;
  }

  // 将 CPU 侧 I420Frame 的 Y/U/V 三平面数据上传到 GPU 的三张纹理中。
  //本会议项目使用最新帧，先把内容更新好,再把最新内容绑定给 shader 使用 因此先update 再bind
  RenderResult texture_result = m_yuv_texture->Update(frame);
  if (texture_result != RenderResult::kSuccess) {
    return false;
  }

  //清屏
  gl->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  gl->glClear(GL_COLOR_BUFFER_BIT);

  //把当前 OpenGL 使用的 shader program 设置为 m_program。
  m_shader->Use();

  // 都是OpenGL状态设置函数：
  // Bind: texture object -> texture unit
  // SetYUVTextureUnits: sampler2D -> texture unit
  m_yuv_texture->Bind(0, 1, 2);
  m_shader->SetYUVTextureUnits(0, 1, 2);

  if (m_video_mesh) {
    //触发的是整条绘制管线
    m_video_mesh->Draw();
  }

  m_yuv_texture->Unbind();
  m_shader->Unuse();

  return true;
}

bool VideoRender::Resize(int width, int height) {
  auto *gl = CurrentOpenGLFunctions();
  if (!gl) {
    return false;
  }

  if (width <= 0 || height <= 0) {
    return false;
  }

  //UI 窗口现在多大，OpenGL 画布就设成多大
  //从当前OpenGL的坐标原点（通常在左下角）画 width*height 大小的区域
  gl->glViewport(0, 0, width, height);
  return true;
}

} // namespace RENDER
