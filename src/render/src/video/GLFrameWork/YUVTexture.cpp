#include "render/src/video/GLFrameWork/YUVTexture.h"
#include "render/include/RenderDefine.h"

#include "render/src/video/GLFrameWork/OpenGLFunctions.h"

namespace RENDER {
//匿名命名空间，让里面的函数只在当前这个 .cpp 文件内部可见，定义YUVTexture自己的工具函数。
namespace {
//对当前绑定到 GL_TEXTURE_2D 的 texture object进行一些配置，主要涉及缩放和越界这两个情况下的处理
void ConfigureTexture(QOpenGLFunctions_3_3_Core *gl) {
  /*
glTexParameteri 设置当前绑定纹理参数的函数：配置放缩和边界时的处理方法
GL_TEXTURE_MIN_FILTER = 缩小时怎么采样
GL_TEXTURE_MAG_FILTER = 放大时怎么采样
GL_TEXTURE_WRAP_S     = 横向越界时怎么处理
GL_TEXTURE_WRAP_T     = 纵向越界时怎么处理

GL_LINEAR：缩放时采用线性插值
GL_CLAMP_TO_EDGE：纹理坐标超出边缘时，使用边缘像素，不重复铺贴。
  */
  gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

//给某一个 Y/U/V 平面对应的 texture object 分配 GPU 存储空间
void AllocatePlaneTexture(QOpenGLFunctions_3_3_Core *gl, unsigned int texture, int width, int height) {
  //把传入的texture绑定到槽位 GL_TEXTURE_2D 上
  gl->glBindTexture(GL_TEXTURE_2D, texture);

  //设置缩放和边缘行为
  ConfigureTexture(gl);

  //对绑定的纹理目标GL_TEXTURE_2D，分配GPU内存，
  /*
  参数解析：
  对槽位GL_TEXTURE_2D上的 texture object 配置：
  1.level：0 不使用mipmap 
  2.通道：GL_RED 取RED作为单通道，因为Y/U/V三个各自对应一条通道
  3.border默认配置为0
  4.format，输入数据的格式：单通道
  5.type：输入数据的数据类型，RenderDeine.h 中已经定义Y/U/V是 uint8_t
  6.pixels：表示cpu侧传入的texture object 对象的指针，这里传nullptr表示先不上传数据。
  */
  gl->glTexImage2D(GL_TEXTURE_2D,
               0,
               GL_RED,
               width,
               height,
               0,
               GL_RED,
               GL_UNSIGNED_BYTE,
               nullptr);
}

//把 CPU 内存里的某个 Y/U/V 平面数据上传到已有 texture object 里
void UpdatePlaneTexture(QOpenGLFunctions_3_3_Core *gl, unsigned int texture, int width, int height, const uint8_t *data) {
  gl->glBindTexture(GL_TEXTURE_2D, texture);
  /*
  参数解析：
  对槽位GL_TEXTURE_2D上对texture object 对象进行处理：
  1.不使用mipmap
  2.从texture的 （x,y) (0,0)也就是左上角/起始位置开始更新。
  3.单通道
  4.说明传入texture object 的数据类型
  5.传入CPU侧的数据指针
  */
  gl->glTexSubImage2D(GL_TEXTURE_2D,
                  0,
                  0,
                  0,
                  width,
                  height,
                  GL_RED,
                  GL_UNSIGNED_BYTE,
                  data);
}

} // namespace

YUVTexture::YUVTexture() = default;

YUVTexture::~YUVTexture() {
  Uninitialize();
}

RenderResult YUVTexture::Initialize(int width, int height) {
  auto *gl = CurrentOpenGLFunctions();
  if (!gl) {
    return RenderResult::kInternalError;
  }

  if (width <= 0 || height <= 0) {
    return RenderResult::kInvalidArgument;
  }

  if (m_initialized) {
    if (width == m_width && height == m_height) {
      return RenderResult::kAlreadyInitialized;
    }
    Uninitialize();
  }

  //设置像素解包对齐。OpenGL默认是4字节，但Y/U/V是1字节，这里就填入1
  //从 CPU 内存上传像素数据到 GPU 时，按 1 字节对齐读取每一行。
  gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  //向 OpenGL 申请生成纹理对象ID，
  //分别为Y/U/V申请texture object ID
  gl->glGenTextures(1, &m_y_texture);
  gl->glGenTextures(1, &m_u_texture);
  gl->glGenTextures(1, &m_v_texture);

  if (m_y_texture == 0 || m_u_texture == 0 || m_v_texture == 0) {
    Uninitialize();
    return RenderResult::kInternalError;
  }

  //4个Y共享一个U和一个V 这就是I420 4:2:0
  //分别为Y/U/V分配GPU内存。
  AllocatePlaneTexture(gl, m_y_texture, width, height);
  AllocatePlaneTexture(gl, m_u_texture, width / 2, height / 2);
  AllocatePlaneTexture(gl, m_v_texture, width / 2, height / 2);
  //清理OpenGL状态
  gl->glBindTexture(GL_TEXTURE_2D, 0);

  m_width = width;
  m_height = height;
  m_initialized = true;
  return RenderResult::kSuccess;
}

void YUVTexture::Uninitialize() {
  if (auto *gl = CurrentOpenGLFunctions()) {
    if (m_y_texture != 0) {
      gl->glDeleteTextures(1, &m_y_texture);
    }
    if (m_u_texture != 0) {
      gl->glDeleteTextures(1, &m_u_texture);
    }
    if (m_v_texture != 0) {
      gl->glDeleteTextures(1, &m_v_texture);
    }
  }

  m_y_texture = 0;
  m_u_texture = 0;
  m_v_texture = 0;
  m_width = 0;
  m_height = 0;
  m_initialized = false;
}

//Update后 GPU 上 Y/U/V 三张 texture object 里的像素内容变成了当前 frame 的 Y/U/V 数据。
//适配视频分辨率变化
RenderResult YUVTexture::Update(const std::shared_ptr<I420Frame> &frame) {
  auto *gl = CurrentOpenGLFunctions();
  if (!gl) {
    return RenderResult::kInternalError;
  }

  if (!frame || !frame->HasData()) {
    return RenderResult::kInvalidArgument;
  }

  //要求数据符合I420 4:2:0
  if (frame->width <= 0 || frame->height <= 0 ||
      frame->width % 2 != 0 || frame->height % 2 != 0) {
    return RenderResult::kInvalidSize;
  }

  if (!m_initialized || frame->width != m_width || frame->height != m_height) {
    // frame 分辨率变化时，需要重新分配 Y/U/V 三张纹理的 GPU 存储空间。
    RenderResult result = Resize(frame->width, frame->height);
    if (result != RenderResult::kSuccess &&
        result != RenderResult::kAlreadyInitialized) {
      return result;
    }
  }

  //// Y/U/V 是单通道 uint8_t 数据，按 1 字节对齐读取 CPU 侧内存。
  gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  //I420要求 Y全宽全高，U/V 半宽半高
  UpdatePlaneTexture(gl, m_y_texture, m_width, m_height, frame->data[0].get());
  UpdatePlaneTexture(gl, m_u_texture, m_width / 2, m_height / 2, frame->data[1].get());
  UpdatePlaneTexture(gl, m_v_texture, m_width / 2, m_height / 2, frame->data[2].get());
  //清理OpenGL状态，不释放纹理资源。
  gl->glBindTexture(GL_TEXTURE_2D, 0);

  return RenderResult::kSuccess;
}

RenderResult YUVTexture::Resize(int width, int height) {
  return Initialize(width, height);
}

/*
  负责完成数据线：Y 平面数据 -> m_y_texture -> texture unit 0
  1.选择槽位 glActiveTexture（）
  2.把texture object 插入槽位里 
  */
void YUVTexture::Bind(int y_unit, int u_unit, int v_unit) {
  auto *gl = CurrentOpenGLFunctions();
  if (!gl) {
    return;
  }

  if (!m_initialized) {
    return;
  }
  //切换当前正在操作的槽位 texture unit。后续 glBindTexture 会作用到 0 号纹理单元
  gl->glActiveTexture(GL_TEXTURE0 + y_unit);

  //把 m_y_texture 这个 texture object，绑定到当前 active texture unit 的 GL_TEXTURE_2D target 上。
  gl->glBindTexture(GL_TEXTURE_2D, m_y_texture);

  gl->glActiveTexture(GL_TEXTURE0 + u_unit);
  gl->glBindTexture(GL_TEXTURE_2D, m_u_texture);

  gl->glActiveTexture(GL_TEXTURE0 + v_unit);
  gl->glBindTexture(GL_TEXTURE_2D, m_v_texture);
}

void YUVTexture::Unbind() {
  auto *gl = CurrentOpenGLFunctions();
  if (!gl) {
    return;
  }

  gl->glActiveTexture(GL_TEXTURE0);
  gl->glBindTexture(GL_TEXTURE_2D, 0);
  gl->glActiveTexture(GL_TEXTURE1);
  gl->glBindTexture(GL_TEXTURE_2D, 0);
  gl->glActiveTexture(GL_TEXTURE2);
  gl->glBindTexture(GL_TEXTURE_2D, 0);
  gl->glActiveTexture(GL_TEXTURE0);
}

} // namespace RENDER
