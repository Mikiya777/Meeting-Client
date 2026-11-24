#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace PLATFORM {

class WasapiDevice {
public:
  enum class Direction { Capture, Playback };
  struct Info {
    std::string id;
    std::string name;
    bool is_default;
  };

  using Callback = std::function<void(float *output, const float *input,
                                      uint32_t frames)>;

  explicit WasapiDevice(Direction direction);
  ~WasapiDevice();

  WasapiDevice(const WasapiDevice &) = delete;
  WasapiDevice &operator=(const WasapiDevice &) = delete;

  std::vector<Info> Devices() const;
  bool Start(const std::string &id, int rate, int channels, Callback callback);
  // Waits for in-flight device callbacks before returning.
  void Stop();

private:
  struct State;
  std::unique_ptr<State> m_state;
};

} // namespace PLATFORM
