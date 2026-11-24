#ifndef VIDEO_MESH_H
#define VIDEO_MESH_H
/*
模块位置：
VideoRender
  -> VideoMesh
  -> OpenGL VAO / VBO / EBO

功能：负责管理绘制视频画面用的 OpenGL 网格。
1.管理 VAO / VBO / EBO
2.准备一个用于承载视频画面的矩形网格
3.在渲染时执行 Draw
*/
namespace RENDER {

class VideoMesh {
public:
  VideoMesh();
  ~VideoMesh();

  //持有 OpenGL 资源 ID，禁止拷贝
  VideoMesh(const VideoMesh &) = delete;
  VideoMesh &operator=(const VideoMesh &) = delete;

  /*
  todo：
1. 定义矩形的顶点坐标
2. 定义纹理坐标
3. 创建 VAO
4. 创建 VBO
5. 创建 EBO
6. 设置顶点属性
  */
  bool Initialize();

  /*
  todo：
  glDeleteVertexArrays
  glDeleteBuffers
  释放：
  m_vao
  m_vbo
  m_ebo
  */
  void Uninitialize();

  /*
  流程中的位置：
  VideoRender::RenderFrame()
  -> YUVTexture 更新纹理
  -> VideoShader::Use()
  -> VideoMesh::Draw()
  -> VideoShader::Unuse()
  */
  void Draw();

private:
  // vao负责记录：位置数据在哪里、纹理坐标在哪里、每个顶点数据步长是多少？
  unsigned int m_vao = 0; //OpenGL VAO ID

  // Vbo负责保存顶点数据：顶点位置，纹理坐标
  unsigned int m_vbo = 0; //OpenGL VBO ID

  // ebo负责保存顶点索引：避免重复写顶点
  unsigned int m_ebo = 0; //OpenGL EBO ID

  bool m_initialized = false; //
};

} // namespace RENDER

#endif // VIDEO_MESH_H
