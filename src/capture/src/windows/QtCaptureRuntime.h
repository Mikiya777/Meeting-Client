#pragma once

#include "capture/include/ICaptureEngine.h"

namespace CAPTURE {

class QtCaptureRuntime final {
public:
  QtCaptureRuntime();
  ~QtCaptureRuntime();

  CaptureResult Init(int sample_rate, int channels, int width, int height, int fps);
  CaptureResult UnInit();

  CaptureResult AddCameraSource(const std::string &name, const std::string &id = {});
  CaptureResult AddMicSource(const std::string &name, const std::string &id = {});
  CaptureResult RemoveSource(const std::string &name);
  CaptureResult UpdateCameraSourceDevice(const std::string &name, const std::string &id);
  CaptureResult UpdateMicSourceDevice(const std::string &name, const std::string &id);

  CaptureResult EnumCameraDevices(CaptureDeviceEnumCallback callback);
  CaptureResult EnumMicDevices(CaptureDeviceEnumCallback callback);
  CaptureResult RegisterVideoCallback(VideoDataCallback callback);
  CaptureResult RegisterAudioCallback(AudioDataCallback callback);

private:
  struct State;
  std::unique_ptr<State> m_state;
};

} // namespace CAPTURE
