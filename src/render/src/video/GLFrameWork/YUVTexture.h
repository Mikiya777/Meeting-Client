#ifndef YUV_TEXTURE_H
#define YUV_TEXTURE_H
/*
模块位置：
VideoRende
  -> YUVTexture
  -> OpenGL texture
功能：
负责管理一帧 I420/YUV420P 视频对应的三个 OpenGL 纹理：
*/

#include "render/include/RenderDefine.h"

#include <memory>

namespace RENDER {

class YUVTexture {
public:
  YUVTexture();
  ~YUVTexture();

  YUVTexture(const YUVTexture &) = delete;
  YUVTexture &operator=(const YUVTexture &) = delete;

  RenderResult Initialize(int width, int height);
  void Uninitialize();

  RenderResult Update(const std::shared_ptr<I420Frame> &frame);
  RenderResult Resize(int width, int height);
  void Bind(int y_unit, int u_unit, int v_unit);
  void Unbind();

private:
  int m_width = 0;
  int m_height = 0;
  unsigned int m_y_texture = 0;
  unsigned int m_u_texture = 0;
  unsigned int m_v_texture = 0;
  bool m_initialized = false;
};

} // namespace RENDER

#endif // YUV_TEXTURE_H
