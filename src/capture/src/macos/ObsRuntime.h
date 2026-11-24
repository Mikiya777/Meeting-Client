#ifndef OBS_RUNTIME_H
#define OBS_RUNTIME_H

#include "capture/include/CaptureDefine.h"
#include "capture/include/ICaptureEngine.h"

#include <memory>
#include <string>
#include <unordered_map>

struct obs_scene;
struct obs_source;
struct obs_scene_item;
struct obs_source_frame;
struct video_data;
struct audio_data;
struct obs_data;

namespace CAPTURE {
class ObsRuntime final {
public:
  ObsRuntime() = default;
  ~ObsRuntime();

  CaptureResult Init(int sample_rate, int channels, int width, int height, int fps);
  CaptureResult UnInit();
  bool IsInitialized() const;

  //源管理，分为默认和指定设备的。
  CaptureResult AddCameraSource(const std::string &name);
  CaptureResult AddMicSource(const std::string &name);
  CaptureResult AddCameraSource(const std::string &name,
                                const std::string &device_id);
  CaptureResult AddMicSource(const std::string &name,
                             const std::string &device_id);
  CaptureResult RemoveSource(const std::string &name);

  //回调注册
  CaptureResult RegisterVideoCallback(VideoDataCallback callback);
  CaptureResult RegisterAudioCallback(AudioDataCallback callback);

  //设备更新
  CaptureResult UpdateCameraSourceDevice(const std::string &name,
                                         const std::string &device_id);
  CaptureResult UpdateMicSourceDevice(const std::string &name,
                                      const std::string &device_id);
  //设备查找
  CaptureResult EnumCameraDevices(CaptureDeviceEnumCallback callback);
  CaptureResult EnumMicDevices(CaptureDeviceEnumCallback callback);
  //帧处理函数
  void HandleSourceVideoFrame(const obs_source_frame *frame);

private:
  // Camera and microphone sources share the same OBS ownership path.
  CaptureResult AddObsSource(const std::string &source_id,
                             const std::string &name,
                             obs_data *settings,
                             bool attach_frame_filter);
  CaptureResult AddSourceFrameFilter(const std::string &name,
                                     obs_source *source);
  void RemoveSourceFrameFilter(const std::string &name,
                               obs_source *source);
  void ClearSourceFrameFilters();
  void ClearSources();

  void UnregisterCallbacks();

  // OBS C callbacks use static entry points and forward through param.
  static void OnRawVideoFrame(void *param, video_data *frame);
  static void OnRawAudioFrame(void *param, size_t mix_idx, audio_data *data);

  void HandleRawVideoFrame(video_data *frame);
  void HandleRawAudioFrame(size_t mix_idx, audio_data *data);

  bool m_initialized = false;
  obs_scene *m_default_scene = nullptr;

  int m_sample_rate = 0;
  int m_channels = 0;
  int m_width = 0;
  int m_height = 0;
  int m_fps = 0;

  std::unordered_map<std::string, obs_source *> m_sources;
  std::unordered_map<std::string, obs_scene_item *> m_scene_items;
  std::unordered_map<std::string, obs_source *> m_source_frame_filters;

  // Access only through atomic_load/store. A frame keeps its snapshot alive
  // even when the control thread replaces or clears the registered callback.
  std::shared_ptr<const VideoDataCallback> m_video_callback;
  std::shared_ptr<const AudioDataCallback> m_audio_callback;

  bool m_audio_callback_registered = false;

  std::shared_ptr<I420Frame> ConvertVideoFrame(const video_data *frame) const;
  std::shared_ptr<AudioFrame> ConvertAudioFrame(const audio_data *data) const;
};
} // namespace CAPTURE

#endif // OBS_RUNTIME_H
