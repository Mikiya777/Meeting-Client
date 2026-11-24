#ifndef VIDEO_RENDER_H
#define VIDEO_RENDER_H

/*
模块位置：
RenderEngine
  -> UserContext
      -> VideoRender
           ->YUVTexture   -> 管理 Y/U/V 三个纹理
           ->VideoShader  -> 管理 shader，后面做 YUV -> RGB
           ->VideoMesh    -> 管理绘制用的四边形网格

功能：
绘制器
它只负责这一个用户的一块视频画面如何真正被画出来，不管多用户，网络之类的事情。
VideoRender 接收一帧 I420Frame，然后协调纹理、着色器、网格，把这个用户的画面画到当前 OpenGL
*/
#include "render/include/RenderDefine.h"

#include <memory>
#include <string>

namespace RENDER {

class VideoMesh;
class VideoShader;
class YUVTexture;

class VideoRender {
public:
  VideoRender(std::string user_name, std::shared_ptr<VideoShader> shared_shader);
  ~VideoRender();

  VideoRender(const VideoRender &) = delete;
  VideoRender &operator=(const VideoRender &) = delete;

  bool Initialize(int video_width, int video_height);
  void Uninitialize();

  bool RenderFrame(const std::shared_ptr<I420Frame> &frame);
  //当 UI 窗口大小变化时，更新渲染尺寸。
  bool Resize(int width, int height);

private:
  std::string m_user_name;
  std::shared_ptr<VideoShader> m_shader;
  std::unique_ptr<YUVTexture> m_yuv_texture;
  std::unique_ptr<VideoMesh> m_video_mesh;
  bool m_initialized = false;
};

} // namespace RENDER

#endif // VIDEO_RENDER_H
