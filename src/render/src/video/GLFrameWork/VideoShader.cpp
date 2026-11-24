#include "render/src/video/GLFrameWork/VideoShader.h"
#include "render/include/RenderDefine.h"

#include "render/src/video/GLFrameWork/OpenGLFunctions.h"

#include "render/src/video/GLFrameWork/ShaderSourceCodes.h"

#include <iostream>

namespace RENDER {
namespace {

//把一段 GLSL 源码编译成一个 OpenGL shader object。
unsigned int CompileShader(QOpenGLFunctions_3_3_Core *gl, unsigned int shader_type, std::string_view shader_source) {
  unsigned int shader = gl->glCreateShader(shader_type);

  //获取源码起始地址和源码长度
  const char *source = shader_source.data();
  int length = static_cast<int>(shader_source.size());

  //将源码交给OpenGL
  //把 source 指向的 GLSL 源码，设置到 shader 这个 shader object 里。
  gl->glShaderSource(shader, 1, &source, &length);
  //编译源码
  gl->glCompileShader(shader);

  int success = 0;
  //查询编译结果
  gl->glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

  //失败处理
  if (!success) {
    char info_log[512] = {};
    gl->glGetShaderInfoLog(shader, sizeof(info_log), nullptr, info_log);
    std::cerr << "Compile shader failed: " << info_log << std::endl;

    gl->glDeleteShader(shader);
    return 0;
  }

  return shader;
}

} // namespace

VideoShader::VideoShader() = default;

VideoShader::~VideoShader() {
  Uninitialize();
}

//OpenGL shader 编译需要当前线程有有效 OpenGL 上下文，在此处进行
/*
  1. 读取或拿到 vertex shader 源码
  2. 读取或拿到 fragment shader 源码
  3. glCreateShader
  4. glShaderSource
  5. glCompileShader
  6. glCreateProgram
  7. glAttachShader
  8. glLinkProgram
  9. 保存 program ID 到 m_program  
  */
RenderResult VideoShader::Initialize() {
  auto *gl = CurrentOpenGLFunctions();
  if (!gl) {
    return RenderResult::kInternalError;
  }

  if (m_initialized) {
    return RenderResult::kAlreadyInitialized;
  }

  //编译 vertex shader
  unsigned int vertex_shader = CompileShader(gl, GL_VERTEX_SHADER, kI420VertexShaderSource);
  if (vertex_shader == 0) {
    return RenderResult::kInternalError;
  }

  //编译fragment shader
  unsigned int fragment_shader =
      CompileShader(gl, GL_FRAGMENT_SHADER, kI420FragmentShaderSource);
  if (fragment_shader == 0) {
    //失败回滚
    gl->glDeleteShader(vertex_shader);
    return RenderResult::kInternalError;
  }

  //创建 shader program
  unsigned int program = gl->glCreateProgram();
  //链接两个shader组成shader program
  gl->glAttachShader(program, vertex_shader);
  gl->glAttachShader(program, fragment_shader);
  //链接 program，链接阶段会检查两个shader之间的衔接是否正确
  /*
  vertex shader 输出和 fragment shader 输入是否能对上
  shader 各阶段是否能组成完整 program
  uniform / attribute 等资源是否合理
  */
  gl->glLinkProgram(program);

  int success = 0;

  //查询链接是否成功。
  gl->glGetProgramiv(program, GL_LINK_STATUS, &success);

  //清理资源，防止泄漏
  gl->glDeleteShader(vertex_shader);
  gl->glDeleteShader(fragment_shader);

  //回馈错误信息
  if (!success) {
    char info_log[512] = {};
    gl->glGetProgramInfoLog(program, sizeof(info_log), nullptr, info_log);
    std::cerr << "Link shader program failed: " << info_log << std::endl;

    gl->glDeleteProgram(program);
    return RenderResult::kInternalError;
  }

  // CPU 在 OpenGL 中的两类工作之一：状态指挥
  /*
1. glGetUniformLocation(program, "yTexture")
   查询 shader program 中 uniform sampler2D yTexture 的位置编号。
   注意：这里拿到的不是纹理 ID，也不是采样器 ID，而是 uniform location。

2. 后续渲染时会通过 glUniform1i(location, unit)
   设置 sampler2D 对应哪个 texture unit（槽位）。

完整关系：
   yTexture uniform location
      -> glUniform1i(location, 0)
      -> yTexture sampler 的值变成 0
      -> fragment shader 采样时去 texture unit 0
      -> texture unit 0 上绑定真实的 Y texture object
*/
  m_y_texture_location = gl->glGetUniformLocation(program, "yTexture");
  m_u_texture_location = gl->glGetUniformLocation(program, "uTexture");
  m_v_texture_location = gl->glGetUniformLocation(program, "vTexture");

  if (m_y_texture_location < 0 ||
      m_u_texture_location < 0 ||
      m_v_texture_location < 0) {
    gl->glDeleteProgram(program);
    return RenderResult::kInternalError;
  }

  m_program = program;
  m_initialized = true;

  return RenderResult::kSuccess;
}

RenderResult VideoShader::Uninitialize() {
  if (!m_initialized) {
    return RenderResult::kNotInitialized;
  }

  if (auto *gl = CurrentOpenGLFunctions()) {
    gl->glDeleteProgram(m_program);
  }

  m_program = 0;
  m_y_texture_location = -1;
  m_u_texture_location = -1;
  m_v_texture_location = -1;
  m_initialized = false;

  return RenderResult::kSuccess;
}

void VideoShader::Use() {
  auto *gl = CurrentOpenGLFunctions();
  if (!gl) {
    return;
  }

  if (!m_initialized) {
    return;
  }

  gl->glUseProgram(m_program);
}

void VideoShader::Unuse() {
  auto *gl = CurrentOpenGLFunctions();
  if (!gl) {
    return;
  }

  gl->glUseProgram(0);
}

void VideoShader::SetYUVTextureUnits(int y_unit, int u_unit, int v_unit) {
  auto *gl = CurrentOpenGLFunctions();
  if (!gl) {
    return;
  }

  if (!m_initialized) {
    return;
  }

  gl->glUniform1i(m_y_texture_location, y_unit);
  gl->glUniform1i(m_u_texture_location, u_unit);
  gl->glUniform1i(m_v_texture_location, v_unit);
}

bool VideoShader::IsInitialized() const {
  return m_initialized;
}

} // namespace RENDER
