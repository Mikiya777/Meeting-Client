#ifndef VCE_ENGINE_IMPL_H
#define VCE_ENGINE_IMPL_H

#include "engine/private/capture/ICaptureDataCallback.h"
#include "engine/private/transport/ITransportDataCallback.h"
#include "engine/private/service/IServiceDataCallback.h"
#include "engine/public/VceTypes.h"

#include "OpenCV-美颜/BeautyProcessor.h"

#include <mutex>
#include <memory>
#include <string>
#include <vector>

namespace VCE {
/*
使用前向声明而不是引用文件，原因：.h 文件应该尽量依赖抽象、隐藏实现。
VceEngineImpl.h 只需要知道“有一个 RenderManager 接口类型”，不需要知道 mock 的具体实现。
头文件依赖抽象，实现文件选择具体实现
*/
class ICaptureManager;
class IRenderManager;
class IMeetingEventObserver;
class ITransportManager;
class ITransportDataCallback;
class IServiceManager;
class IServiceDataCallback;
class AudioProcessingPipeline;

class VceEngineImpl
    : public ICaptureDataCallback,
      public ITransportDataCallback,
      public IServiceDataCallback,
      public std::enable_shared_from_this<VceEngineImpl> {
public:
  VceEngineImpl();
  VceEngineImpl(std::unique_ptr<ICaptureManager> capture_manager,
                std::unique_ptr<IRenderManager> render_manager,
                std::unique_ptr<ITransportManager> transport_manager,
                std::unique_ptr<IServiceManager> service_manager);
  ~VceEngineImpl();

  Result Init();
  Result Uninit();

  Result OpenMicrophone();
  Result CloseMicrophone();

  Result OpenCamera();
  Result CloseCamera();

  // Fixed light smoothing; disabled by default.
  Result SetBeautyEnabled(bool enabled);

  Result GetMicrophoneDevices(std::vector<MediaDeviceInfo> *devices);
  Result GetCameraDevices(std::vector<MediaDeviceInfo> *devices);
  Result SetMicrophoneDevice(const std::string &device_id);
  Result SetCameraDevice(const std::string &device_id);

  Result AddRenderUser(const std::string &user_id, bool is_local);
  Result RemoveRenderUser(const std::string &user_id);
  Result RenderUser(const std::string &user_id);
  Result UpdateUserVideoSize(const std::string &user_id, int width, int height);

  Result RegisterUser(const std::string &username,
                      const std::string &password,
                      uint32_t client_ip = 0,
                      ServiceUserInfo *user_info = nullptr);
  Result Login(const std::string &username,
               const std::string &password,
               uint32_t client_ip = 0,
               ServiceUserInfo *user_info = nullptr);
  Result CreateMeeting(const std::string &title,
                       const std::string &description,
                       int64_t start_time,
                       MeetingInfo *meeting_info = nullptr);
  Result JoinMeeting(const std::string &meeting_id,
                     MeetingInfo *meeting_info = nullptr);
  Result LeaveMeeting();
  Result EndMeeting();
  Result ListMeetings(bool active_only,
                      std::vector<MeetingInfo> *meetings);

  EngineState GetState() const;
  ServiceUserInfo GetCurrentUserInfo() const;
  MeetingInfo GetCurrentMeetingInfo() const;
  TransportRoomInfo GetCurrentTransportRoomInfo() const;

  void AddObserver(
      const std::shared_ptr<IMeetingEventObserver> &observer);

  void RemoveObserver(
      const std::shared_ptr<IMeetingEventObserver> &observer);

private:
  Result InitManagers();
  Result UninitManagers();
  bool IsInitialized() const;
  Result ApplyMeetingTransportInfo(const MeetingInfo &meeting_info);
  void ClearCurrentMeetingTransport();

  //观察者
  void NotifyEngineStateChanged(EngineState state);
  void NotifyMicrophoneStateChanged(bool enabled);
  void NotifyCameraStateChanged(bool enabled);
  void NotifyTransportConnectionStateChanged(bool connected);
  void NotifyRenderVideoFrameReady(const std::string &user_id);

  std::vector<std::shared_ptr<IMeetingEventObserver>> GetActiveObservers();

  //Capture Module
  // ICaptureDataCallback 回调入口
  void OnCaptureVideoFrame(const std::shared_ptr<I420Frame> &frame) override;
  void OnCaptureAudioFrame(const std::shared_ptr<AudioFrame> &frame) override;

  //Tansport Module
  // ITansportDataCallback 回调入口
  void OnTransportVideoFrame(const std::string &user_id,
                             const std::shared_ptr<I420Frame> &frame) override;
  void OnTransportAudioFrame(const std::string &user_id,
                             const std::shared_ptr<AudioFrame> &frame) override;
  void OnTransportConnectionStateChanged(TransportState state) override;

  //Service Module
  // Engine 内部通知 observer
  void NotifyUserJoined(const ServiceUserInfo &user);
  void NotifyUserLeft(const std::string &user_id);
  void NotifyMeetingEnded();

  // IServiceDataCallback 回调入口
  void OnUserJoined(const ServiceUserInfo &user) override;
  void OnUserLeft(const std::string &user_id) override;
  void OnMeetingEnded() override;

private:
  //以后 Engine 的方法可能被 UI 线程、采集线程和网络线程同时调用，因此状态需要锁保护。
  //锁不属于业务状态，即使在 const 方法里也允许使用。(修改)
  //多个线程会同时访问同一个对象里的共享数据，因此需要用到锁；共享数据其实就是：
  // 1.引擎对象的状态（m_state_mutex - m_state）
  // 2.观察者列表（std::vector<std::weak_ptr<IMeetingEventObserver>> m_observers;）
  // 3.流程 （m_operation_mutex）
  //
  //状态：Init()、Uninit()、OpenCamera()、CloseCamera()、OpenMicrophone()、CloseMicrophone()、GetState()
  //观察者：UI：AddObserver()、RemoveObserver() Engine:NotifyCameraStateChanged()、NotifyEngineStateChanged()
  //流程：Init()、Uninit()、OpenCamera()、CloseCamera() 线程 A: OpenCamera 通过状态检查、线程 B: Uninit 开始关闭模块、线程 A: 继续调用 camera open

  //保护engine状态，即保护m_state
  mutable std::mutex m_state_mutex;

  EngineState m_state{EngineState::kES_Created};

  // 串行化 Init/Uninit/设备开关这类会调用子模块的操作。
  // 子模块调用不放在 m_state_mutex 内，避免真实模块回调 Engine 时形成死锁。
  std::mutex m_operation_mutex;

  //Capture Module
  BeautyProcessor m_beauty_processor;
  std::unique_ptr<ICaptureManager> m_capture_manager;

  //Render Module
  std::unique_ptr<IRenderManager> m_render_manager;

  //Transport Module
  std::unique_ptr<ITransportManager> m_transport_manager;

  //Service Module
  std::unique_ptr<IServiceManager> m_service_manager;
  std::shared_ptr<AudioProcessingPipeline> m_audio_processing_pipeline;
  mutable std::mutex m_session_mutex;
  ServiceUserInfo m_current_user;
  MeetingInfo m_current_meeting;
  std::string m_local_user_id{"local"};

  // Window观察者
  // 单独使用一把锁，避免设备状态锁和观察者锁互相干扰。
  mutable std::mutex m_observer_mutex;
  // Engine 不拥有 UI，所以使用 weak_ptr 防止循环引用。
  std::vector<std::weak_ptr<IMeetingEventObserver>> m_observers;
};
} // namespace VCE

#endif
