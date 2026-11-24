#ifndef VIDEO_SHADER_H
#define VIDEO_SHADER_H
/*
组件位置：
VideoRender
  -> VideoShader
  -> OpenGL shader program

功能：管理 OpenGL shader 程序 把YUV->RGB
1. 创建 shader program
2. 编译 vertex shader
3. 编译 fragment shader
4. 链接 shader program
5. 渲染时启用 shader
6. 渲染结束后取消使用 shader
7. 释放 shader program
*/
#include "render/include/RenderDefine.h"
namespace RENDER {

class VideoShader {
public:
  VideoShader();
  ~VideoShader();

  //持有 OpenGL program ID，禁止拷贝
  VideoShader(const VideoShader &) = delete;
  VideoShader &operator=(const VideoShader &) = delete;

  RenderResult Initialize();
  RenderResult Uninitialize();

  //启用当前 shader program
  /*
  渲染流程：
  m_shader->Use();
  m_yuv_texture->Bind();
  m_video_mesh->Draw();
  m_shader->Unuse();
  */
  void Use();
  void Unuse();

  void SetYUVTextureUnits(int y_unit, int u_unit, int v_unit);

  bool IsInitialized() const;

private:
  unsigned int m_program = 0;

  int m_y_texture_location = -1;
  int m_u_texture_location = -1;
  int m_v_texture_location = -1;

  bool m_initialized = false;
};

} // namespace RENDER

#endif // VIDEO_SHADER_H
