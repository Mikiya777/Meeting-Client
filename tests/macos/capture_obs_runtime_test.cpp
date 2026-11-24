#include "capture/include/CaptureDefine.h"
#include "capture/src/macos/ObsRuntime.h"

#include <obs.h>

#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
#include <string>
#include <vector>

namespace {

bool TestVideoCallbackReplacement(CAPTURE::ObsRuntime &runtime) {
  using CAPTURE::CaptureResult;
  using CAPTURE::VideoDataCallback;

  // Feed the same tiny I420 frame a camera filter would pass to the runtime.
  uint8_t y[] = {16, 16, 16, 16};
  uint8_t u[] = {128};
  uint8_t v[] = {128};
  obs_source_frame frame{};
  frame.width = 2;
  frame.height = 2;
  frame.format = VIDEO_FORMAT_I420;
  frame.data[0] = y;
  frame.data[1] = u;
  frame.data[2] = v;
  frame.linesize[0] = 2;
  frame.linesize[1] = 1;
  frame.linesize[2] = 1;

  int old_calls = 0;
  int new_calls = 0;
  VideoDataCallback new_callback = [&new_calls](const auto &) { ++new_calls; };
  CaptureResult replacement = CaptureResult::kInternalError;
  if (runtime.RegisterVideoCallback([&](const auto &) {
        ++old_calls;
        replacement = runtime.RegisterVideoCallback(new_callback);
      }) != CaptureResult::kSuccess) {
    return false;
  }

  runtime.HandleSourceVideoFrame(&frame);
  runtime.HandleSourceVideoFrame(&frame);
  if (replacement != CaptureResult::kSuccess || old_calls != 1 || new_calls != 1) {
    runtime.RegisterVideoCallback([](const auto &) {});
    return false;
  }

  constexpr int kFrameCount = 2000;
  std::atomic<int> delivered{0};
  auto count_frame = [&delivered](const auto &) { ++delivered; };
  bool registered =
      runtime.RegisterVideoCallback(count_frame) == CaptureResult::kSuccess;
  if (!registered) {
    runtime.RegisterVideoCallback([](const auto &) {});
    return false;
  }

  std::thread capture_thread([&] {
    for (int i = 0; i < kFrameCount; ++i) {
      runtime.HandleSourceVideoFrame(&frame);
    }
  });
  for (int i = 0; i < kFrameCount; ++i) {
    if (runtime.RegisterVideoCallback(count_frame) != CaptureResult::kSuccess) {
      registered = false;
      break;
    }
  }
  capture_thread.join();

  const bool delivered_all = delivered.load() == kFrameCount;
  const auto reset = runtime.RegisterVideoCallback([](const auto &) {});
  return registered && delivered_all && reset == CaptureResult::kSuccess;
}

} // namespace

int main() {
  CAPTURE::ObsRuntime runtime;

  auto result = runtime.AddCameraSource("camera_before_init");
  assert(result == CAPTURE::CaptureResult::kNotInitialized);

  result = runtime.Init(48000, 2, 1280, 720, 30);
  if (result != CAPTURE::CaptureResult::kSuccess || !runtime.IsInitialized()) {
    std::cerr << "OBS initialization failed\n";
    return 1;
  }
  if (!TestVideoCallbackReplacement(runtime)) {
    std::cerr << "Video callback replacement lost or misrouted a frame\n";
    return 1;
  }

  std::vector<std::string> camera_device_ids;
  result = runtime.EnumCameraDevices([&](const char *device_id, const char *device_name) {
    assert(device_id != nullptr);
    assert(device_name != nullptr);
    camera_device_ids.emplace_back(device_id);
    return true;
  });
  assert(result == CAPTURE::CaptureResult::kSuccess);

  std::vector<std::string> mic_device_ids;
  result = runtime.EnumMicDevices([&](const char *device_id, const char *device_name) {
    assert(device_id != nullptr);
    assert(device_name != nullptr);
    mic_device_ids.emplace_back(device_id);
    return true;
  });
  assert(result == CAPTURE::CaptureResult::kSuccess);

  result = runtime.AddCameraSource("");
  assert(result == CAPTURE::CaptureResult::kInvalidArgument);

  result = runtime.AddCameraSource("camera_0");
  assert(result == CAPTURE::CaptureResult::kSuccess);

  result = runtime.AddCameraSource("camera_0");
  assert(result == CAPTURE::CaptureResult::kAlreadyInitialized);

  result = runtime.AddMicSource("mic_0");
  assert(result == CAPTURE::CaptureResult::kSuccess);

  if (!camera_device_ids.empty()) {
    result = runtime.AddCameraSource("camera_selected_0", camera_device_ids.front());
    assert(result == CAPTURE::CaptureResult::kSuccess);

    const std::string &updated_camera_id =
        camera_device_ids.size() > 1 ? camera_device_ids[1] : camera_device_ids.front();
    result = runtime.UpdateCameraSourceDevice("camera_selected_0",
                                              updated_camera_id);
    assert(result == CAPTURE::CaptureResult::kSuccess);

    result = runtime.RemoveSource("camera_selected_0");
    assert(result == CAPTURE::CaptureResult::kSuccess);
  }

  if (!mic_device_ids.empty()) {
    result = runtime.AddMicSource("mic_selected_0", mic_device_ids.front());
    assert(result == CAPTURE::CaptureResult::kSuccess);

    const std::string &updated_mic_id =
        mic_device_ids.size() > 1 ? mic_device_ids[1] : mic_device_ids.front();
    result = runtime.UpdateMicSourceDevice("mic_selected_0",
                                           updated_mic_id);
    assert(result == CAPTURE::CaptureResult::kSuccess);

    result = runtime.RemoveSource("mic_selected_0");
    assert(result == CAPTURE::CaptureResult::kSuccess);
  }

  result = runtime.RemoveSource("camera_0");
  assert(result == CAPTURE::CaptureResult::kSuccess);

  result = runtime.RemoveSource("camera_0");
  assert(result == CAPTURE::CaptureResult::kNotFound);

  result = runtime.RemoveSource("mic_0");
  assert(result == CAPTURE::CaptureResult::kSuccess);

  result = runtime.UnInit();
  assert(result == CAPTURE::CaptureResult::kSuccess);
  assert(!runtime.IsInitialized());

  return 0;
}
