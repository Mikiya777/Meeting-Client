#ifndef USER_CONTEXT_H
#define USER_CONTEXT_H
/*
模块位置：
RenderEngine
  -> UserContext
      -> VideoRender
          -> YUVTexture / VideoShader / VideoMesh
功能：
视频帧来源和选帧器
1. 记录这个用户是谁
2. 缓存这个用户的视频帧
3. 调用 VideoRender 渲染这个用户的一帧画面
*/
#include "render/include/RenderDefine.h"

#include <memory>
#include <mutex>
#include <string>

namespace RENDER {

class VideoRender;
class VideoShader;

class UserContext {
public:
  UserContext(std::string user_name, bool is_local);
  ~UserContext();

  //禁用拷贝
  /*
  UserContext包含以下资源，不允许复制
  std::mutex
  VideoRender
  视频帧缓存
  OpenGL 相关资源
  */
  UserContext(const UserContext &) = delete;
  UserContext &operator=(const UserContext &) = delete;

  bool Initialize(std::shared_ptr<VideoShader> shader, int video_width, int video_height);
  void Uninitialize();

  //把视频帧放进这个用户自己的缓存里
  void PushFrame(const std::shared_ptr<I420Frame> &frame);
  //渲染这个用户的一帧画面。
  bool RenderFrame();
  //更新这个用户视频画面的渲染尺寸。
  bool UpdateVideoSize(int width, int height);

  const std::string &GetUserName() const;
  bool IsLocalUser() const;

private:
  std::shared_ptr<I420Frame> PopFrameForRender();

private:
  /*
  每个用户都有自己的：
    视频帧缓存
    上一帧
    本地/远端标记
    VideoRender
*/
  std::string m_user_name;
  bool m_is_local = false;
  std::shared_ptr<VideoRender> m_video_render;

  // 两次绘制之间只保留时间戳最新的待绘制帧。
  std::shared_ptr<I420Frame> m_pending_frame;

  // 没有新帧可渲染时复用上一帧，避免画面闪黑或空白。
  std::shared_ptr<I420Frame> m_last_frame;

  /*
    PushFrame 可能在网络/采集线程调用
    RenderFrame 可能在 UI/OpenGL 线程调用
  */
  std::mutex m_frame_buffer_mutex;
};

} // namespace RENDER

#endif // USER_CONTEXT_H
