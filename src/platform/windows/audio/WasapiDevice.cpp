#include "platform/windows/audio/WasapiDevice.h"

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace PLATFORM {
namespace {

std::string DeviceId(const ma_device_id &id) {
  // Encode the WASAPI endpoint ID as ASCII so it can travel through the SDK.
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  for (const auto unit : id.wasapi) {
    if (unit == 0) {
      break;
    }
    const auto value = static_cast<uint16_t>(unit);
    result += hex[(value >> 12) & 15];
    result += hex[(value >> 8) & 15];
    result += hex[(value >> 4) & 15];
    result += hex[value & 15];
  }
  return result;
}

} // namespace

struct WasapiDevice::State {
  explicit State(Direction direction)
      : capture(direction == Direction::Capture) {
    const ma_backend backend = ma_backend_wasapi;
    ready = ma_context_init(&backend, 1, nullptr, &context) == MA_SUCCESS;
  }

  ~State() {
    if (ready) {
      ma_context_uninit(&context);
    }
  }

  std::vector<ma_device_info> Devices() {
    ma_device_info *devices = nullptr;
    ma_uint32 count = 0;
    if (!ready || ma_context_get_devices(
        &context, capture ? nullptr : &devices, capture ? nullptr : &count,
        capture ? &devices : nullptr, capture ? &count : nullptr) != MA_SUCCESS ||
        count == 0) {
      return {};
    }
    return {devices, devices + count};
  }

  static void OnData(ma_device *device, void *output, const void *input,
                     ma_uint32 frames) noexcept {
    auto &state = *static_cast<State *>(device->pUserData);
    auto *samples = static_cast<float *>(output);
    try {
      state.callback(samples, static_cast<const float *>(input), frames);
    } catch (...) {
      if (samples) {
        std::fill_n(samples, static_cast<size_t>(frames) * device->playback.channels, 0.0f);
      }
      std::fputs("WASAPI: audio callback failed\n", stderr);
    }
  }

  const bool capture;
  bool ready = false;
  bool open = false;
  ma_context context{};
  ma_device device{};
  Callback callback;
};

WasapiDevice::WasapiDevice(Direction direction)
    : m_state(std::make_unique<State>(direction)) {}

WasapiDevice::~WasapiDevice() {
  Stop();
}

std::vector<WasapiDevice::Info> WasapiDevice::Devices() const {
  std::vector<Info> result;
  for (const auto &device : m_state->Devices()) {
    result.push_back({DeviceId(device.id), device.name, device.isDefault != 0});
  }
  return result;
}

bool WasapiDevice::Start(const std::string &id, int rate, int channels,
                         Callback callback) {
  auto &state = *m_state;
  if (!state.ready || state.open || rate <= 0 || channels <= 0 || !callback) {
    return false;
  }

  ma_device_id endpoint{};
  const ma_device_id *selected = nullptr;
  if (!id.empty() && id != "default") {
    const auto devices = state.Devices();
    const auto match = std::find_if(devices.begin(), devices.end(), [&](const auto &device) {
      return DeviceId(device.id) == id;
    });
    if (match == devices.end()) {
      return false;
    }
    endpoint = match->id;
    selected = &endpoint;
  }

  auto config = ma_device_config_init(state.capture ? ma_device_type_capture
                                                     : ma_device_type_playback);
  config.sampleRate = static_cast<ma_uint32>(rate);
  config.periodSizeInMilliseconds = 20;
  config.dataCallback = &State::OnData;
  config.pUserData = &state;
  if (state.capture) {
    config.capture.format = ma_format_f32;
    config.capture.channels = static_cast<ma_uint32>(channels);
    config.capture.pDeviceID = selected;
  } else {
    config.playback.format = ma_format_f32;
    config.playback.channels = static_cast<ma_uint32>(channels);
    config.playback.pDeviceID = selected;
  }
  const auto init_result = ma_device_init(&state.context, &config, &state.device);
  if (init_result != MA_SUCCESS) {
    std::fprintf(stderr, "WASAPI: device initialization failed (%d)\n", init_result);
    return false;
  }

  state.open = true;
  state.callback = std::move(callback);
  const auto start_result = ma_device_start(&state.device);
  if (start_result != MA_SUCCESS) {
    std::fprintf(stderr, "WASAPI: device start failed (%d)\n", start_result);
    Stop();
    return false;
  }
  return true;
}

void WasapiDevice::Stop() {
  auto &state = *m_state;
  if (state.open) {
    ma_device_uninit(&state.device);
    state.open = false;
    state.callback = {};
  }
}

} // namespace PLATFORM
