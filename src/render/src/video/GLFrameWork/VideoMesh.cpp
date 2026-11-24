#include "render/src/video/GLFrameWork/VideoMesh.h"

#include "render/src/video/GLFrameWork/OpenGLFunctions.h"

/*
Mesh,负责配置绘制什么几何图形的问题。
它需要使用到顶点和纹理坐标，（顶点和纹理成对出现）
*/

namespace RENDER {
namespace {

// 每四个数据为一个顶点，x,y,u,v
// x,y表示OpenGL屏幕里的位置，u,v表示这个顶点对应纹理图像上的位置。
constexpr float kVertices[] = {
    -1.0f,
    1.0f,
    0.0f,
    0.0f,
    -1.0f,
    -1.0f,
    0.0f,
    1.0f,
    1.0f,
    -1.0f,
    1.0f,
    1.0f,
    1.0f,
    1.0f,
    1.0f,
    0.0f,
};

//索引数据，三个数据为一组，按这里的顺序确定一个三角形（OpenGL的最基本绘制几何单元）
//0 左上 1 左下 2 右下 3 右上
constexpr unsigned int kIndices[] = {
    0,
    1,
    2,
    0,
    2,
    3,
};

} // namespace

VideoMesh::VideoMesh() = default;

VideoMesh::~VideoMesh() {
  Uninitialize();
}

bool VideoMesh::Initialize() {
  auto *gl = CurrentOpenGLFunctions();
  if (!gl) {
    return false;
  }

  if (m_initialized) {
    return true;
  }

  //申请三个对象ID
  gl->glGenVertexArrays(1, &m_vao);
  gl->glGenBuffers(1, &m_vbo);
  gl->glGenBuffers(1, &m_ebo);

  if (m_vao == 0 || m_vbo == 0 || m_ebo == 0) {
    Uninitialize();
    return false;
  }

  //开始配置VAO
  gl->glBindVertexArray(m_vao);

  //绑定VBO到buffer上，然后把CPU侧的kVertices记录的顶点数据传入GPU
  gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
  gl->glBufferData(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices, GL_STATIC_DRAW);

  //绑定EBO到buffer上，然后把kIndices记录的索引信息传入GPU
  gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
  gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kIndices), kIndices, GL_STATIC_DRAW);

  //配置几何顶点解读信息
  /*参数解析：
    index：shader attribute location 表示 shader 顶点属性 location 编号，这里 0 对应 layout(location = 0) in vec2 aPos;
    size：分量个数，这里是两个分量，aPos = x, y
    type：分量类型，类型是float
    normalized：是否归一化（此处数据都是预设好的不超过[-1,1]，不用归一化），
    stride：步长，4个float读一次
    pointer：偏移量，表示从一个步长里的0号开始取size个分量作为aPos
  
  总结：
    每隔 4 个 float 取一次
    从偏移 0 开始 （这四个float作为的一个整体的0号位置开始）
    每次取 2 个 float
    作为 aPos （x,y）
  */
  gl->glVertexAttribPointer(0,
                        2,
                        GL_FLOAT,
                        GL_FALSE,
                        4 * sizeof(float),
                        reinterpret_cast<void *>(0));
  //启用
  gl->glEnableVertexAttribArray(0);

  //配置纹理顶点解读信息
  /*
  参数解析：
    index：shader attribute location 表示 shader 顶点属性 location 编号，这里 1 对应 layout(location = 1) in vec2 aTexCoord;
    size：分量个数，这里是两个分量
    type：分量类型，类型是float
    normalized：是否归一化（此处数据都是预设好的不超过[-1,1]，不用归一化），
    stride：步长，4个float读一次
    pointer：偏移量，表示从一个步长里的0号开始取size个分量作为aTexCoord
  */
  gl->glVertexAttribPointer(1,
                        2,
                        GL_FLOAT,
                        GL_FALSE,
                        4 * sizeof(float),
                        reinterpret_cast<void *>(2 * sizeof(float)));
  //启用
  gl->glEnableVertexAttribArray(1);

  //关闭配置
  gl->glBindVertexArray(0);
  gl->glBindBuffer(GL_ARRAY_BUFFER, 0);

  m_initialized = true;
  return true;
}

void VideoMesh::Uninitialize() {
  if (auto *gl = CurrentOpenGLFunctions()) {
    if (m_ebo != 0) {
      gl->glDeleteBuffers(1, &m_ebo);
    }
    if (m_vbo != 0) {
      gl->glDeleteBuffers(1, &m_vbo);
    }
    if (m_vao != 0) {
      gl->glDeleteVertexArrays(1, &m_vao);
    }
  }

  m_vao = 0;
  m_vbo = 0;
  m_ebo = 0;
  m_initialized = false;
}

void VideoMesh::Draw() {
  auto *gl = CurrentOpenGLFunctions();
  if (!gl) {
    return;
  }

  if (!m_initialized) {
    return;
  }

  //使用这个VAO
  gl->glBindVertexArray(m_vao);

  //在绘制时加入EBO的解读信息
  /*
  参数解析：
  mode：绘制模式是 GL_TRIANGLES 三角形
  count：索引数 6个 EBO正好六个索引。
  type：UNSIGNED_INT，索引数据的类型
  nullptr：从当前绑定的 EBO 的起始位置开始读取索引
  */
  gl->glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
  //停止使用
  gl->glBindVertexArray(0);
}

} // namespace RENDER
