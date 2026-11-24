#include "render/src/audio/AudioRender.h"

#include "platform/windows/audio/WasapiDevice.h"

namespace RENDER {

std::vector<AudioSpeaker> AudioRender::GetSpeakerDevices() const {
  std::vector<AudioSpeaker> devices{{"default", "Default Speaker", true}};
  PLATFORM::WasapiDevice playback(PLATFORM::WasapiDevice::Direction::Playback);
  for (const auto &device : playback.Devices()) {
    devices.push_back({device.id, device.name, false});
  }
  return devices;
}

bool AudioRender::OpenPlaybackDevice(const std::string &device_id) {
  auto device = std::make_unique<PLATFORM::WasapiDevice>(
      PLATFORM::WasapiDevice::Direction::Playback);
  if (!device->Start(device_id, m_sample_rate, m_channels,
                     [this](float *output, const float *, uint32_t frame_count) {
                       FillPlaybackBuffer(output, frame_count);
                     })) {
    return false;
  }

  m_playback_device = std::move(device);
  m_current_speaker_id = device_id.empty() ? "default" : device_id;
  return true;
}

void AudioRender::ClosePlaybackDevice() {
  m_playback_device.reset();
}

} // namespace RENDER
