#include "render/src/audio/AudioRender.h"

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <cstdlib>

namespace RENDER {
namespace {

constexpr int kPlaybackBufferMilliseconds = 20;
constexpr int kPlaybackBufferCount = 3;

AudioDeviceID GetDefaultOutputDevice() {
  AudioDeviceID device_id = kAudioObjectUnknown;
  UInt32 data_size = sizeof(device_id);
  AudioObjectPropertyAddress address{
      kAudioHardwarePropertyDefaultOutputDevice,
      kAudioObjectPropertyScopeGlobal,
      kAudioObjectPropertyElementMain};

  if (AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                 &address,
                                 0,
                                 nullptr,
                                 &data_size,
                                 &device_id) != noErr) {
    return kAudioObjectUnknown;
  }

  return device_id;
}

bool DeviceHasOutputChannels(AudioDeviceID device_id) {
  AudioObjectPropertyAddress address{
      kAudioDevicePropertyStreamConfiguration,
      kAudioDevicePropertyScopeOutput,
      kAudioObjectPropertyElementMain};

  UInt32 data_size = 0;
  if (AudioObjectGetPropertyDataSize(device_id, &address, 0, nullptr, &data_size) != noErr ||
      data_size == 0) {
    return false;
  }

  std::vector<uint8_t> storage(data_size);
  auto *buffer_list = reinterpret_cast<AudioBufferList *>(storage.data());
  if (AudioObjectGetPropertyData(device_id,
                                 &address,
                                 0,
                                 nullptr,
                                 &data_size,
                                 buffer_list) != noErr) {
    return false;
  }

  for (UInt32 i = 0; i < buffer_list->mNumberBuffers; ++i) {
    if (buffer_list->mBuffers[i].mNumberChannels > 0) {
      return true;
    }
  }

  return false;
}

std::string GetDeviceName(AudioDeviceID device_id) {
  CFStringRef device_name = nullptr;
  UInt32 data_size = sizeof(device_name);
  AudioObjectPropertyAddress address{
      kAudioObjectPropertyName,
      kAudioObjectPropertyScopeGlobal,
      kAudioObjectPropertyElementMain};

  if (AudioObjectGetPropertyData(device_id,
                                 &address,
                                 0,
                                 nullptr,
                                 &data_size,
                                 &device_name) != noErr ||
      device_name == nullptr) {
    return "Speaker " + std::to_string(device_id);
  }

  char name_buffer[256] = {};
  const bool converted = CFStringGetCString(device_name,
                                            name_buffer,
                                            sizeof(name_buffer),
                                            kCFStringEncodingUTF8);
  CFRelease(device_name);

  return converted ? std::string(name_buffer)
                   : "Speaker " + std::to_string(device_id);
}

AudioDeviceID ParseDeviceId(const std::string &device_id) {
  if (device_id.empty() || device_id == "default") {
    return GetDefaultOutputDevice();
  }

  char *end = nullptr;
  const unsigned long value = std::strtoul(device_id.c_str(), &end, 10);
  if (end == device_id.c_str() || *end != '\0') {
    return kAudioObjectUnknown;
  }

  return static_cast<AudioDeviceID>(value);
}

} // namespace

std::vector<AudioSpeaker> AudioRender::GetSpeakerDevices() const {
  std::vector<AudioSpeaker> devices;
  const AudioDeviceID default_device = GetDefaultOutputDevice();

  AudioObjectPropertyAddress address{
      kAudioHardwarePropertyDevices,
      kAudioObjectPropertyScopeGlobal,
      kAudioObjectPropertyElementMain};

  UInt32 data_size = 0;
  if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,
                                     &address,
                                     0,
                                     nullptr,
                                     &data_size) != noErr ||
      data_size == 0) {
    return {AudioSpeaker{"default", "Default Speaker", true}};
  }

  std::vector<AudioDeviceID> device_ids(data_size / sizeof(AudioDeviceID));
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                 &address,
                                 0,
                                 nullptr,
                                 &data_size,
                                 device_ids.data()) != noErr) {
    return {AudioSpeaker{"default", "Default Speaker", true}};
  }

  devices.push_back(AudioSpeaker{"default", "Default Speaker", true});
  for (const AudioDeviceID device_id : device_ids) {
    if (!DeviceHasOutputChannels(device_id)) {
      continue;
    }

    devices.push_back(AudioSpeaker{
        std::to_string(device_id),
        GetDeviceName(device_id),
        device_id == default_device});
  }

  return devices;
}

void AudioRender::AudioQueueOutputCallback(void *user_data,
                                           AudioQueueRef /*queue*/,
                                           AudioQueueBufferRef buffer) {
  auto *audio_render = static_cast<AudioRender *>(user_data);
  if (!audio_render || !buffer) {
    return;
  }

  audio_render->FillAndEnqueueBuffer(buffer);
}

bool AudioRender::OpenPlaybackDevice(const std::string &device_id) {
  const AudioDeviceID output_device = ParseDeviceId(device_id);
  if (output_device == kAudioObjectUnknown) {
    return false;
  }

  AudioStreamBasicDescription format{};
  format.mSampleRate = static_cast<Float64>(m_sample_rate);
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  format.mBytesPerPacket = static_cast<UInt32>(sizeof(float) * m_channels);
  format.mFramesPerPacket = 1;
  format.mBytesPerFrame = static_cast<UInt32>(sizeof(float) * m_channels);
  format.mChannelsPerFrame = static_cast<UInt32>(m_channels);
  format.mBitsPerChannel = static_cast<UInt32>(sizeof(float) * 8);

  AudioQueueRef queue = nullptr;
  OSStatus status = AudioQueueNewOutput(&format,
                                        &AudioRender::AudioQueueOutputCallback,
                                        this,
                                        nullptr,
                                        nullptr,
                                        0,
                                        &queue);
  if (status != noErr || queue == nullptr) {
    return false;
  }

  if (!device_id.empty() && device_id != "default") {
    status = AudioQueueSetProperty(queue,
                                   kAudioQueueProperty_CurrentDevice,
                                   &output_device,
                                   sizeof(output_device));
    if (status != noErr) {
      AudioQueueDispose(queue, true);
      return false;
    }
  }

  m_audio_queue = queue;
  m_buffer_frame_count =
      static_cast<uint32_t>(std::max(1, m_sample_rate * kPlaybackBufferMilliseconds / 1000));
  m_buffer_byte_size =
      m_buffer_frame_count * static_cast<uint32_t>(m_channels) * sizeof(float);

  m_audio_buffers.clear();
  m_audio_buffers.reserve(kPlaybackBufferCount);
  for (int i = 0; i < kPlaybackBufferCount; ++i) {
    AudioQueueBufferRef buffer = nullptr;
    status = AudioQueueAllocateBuffer(m_audio_queue, m_buffer_byte_size, &buffer);
    if (status != noErr || buffer == nullptr) {
      ClosePlaybackDevice();
      return false;
    }

    m_audio_buffers.push_back(buffer);
    if (!FillAndEnqueueBuffer(buffer)) {
      ClosePlaybackDevice();
      return false;
    }
  }

  status = AudioQueueStart(m_audio_queue, nullptr);
  if (status != noErr) {
    ClosePlaybackDevice();
    return false;
  }

  m_current_speaker_id = device_id.empty() ? "default" : device_id;
  return true;
}

void AudioRender::ClosePlaybackDevice() {
  if (!m_audio_queue) {
    m_audio_buffers.clear();
    return;
  }

  AudioQueueStop(m_audio_queue, true);
  for (AudioQueueBufferRef buffer : m_audio_buffers) {
    if (buffer) {
      AudioQueueFreeBuffer(m_audio_queue, buffer);
    }
  }
  m_audio_buffers.clear();
  AudioQueueDispose(m_audio_queue, true);
  m_audio_queue = nullptr;
  m_buffer_frame_count = 0;
  m_buffer_byte_size = 0;
}

bool AudioRender::FillAndEnqueueBuffer(AudioQueueBufferRef buffer) {
  if (!m_audio_queue || !buffer || m_buffer_frame_count == 0) {
    return false;
  }

  FillPlaybackBuffer(static_cast<float *>(buffer->mAudioData), m_buffer_frame_count);
  buffer->mAudioDataByteSize = m_buffer_byte_size;
  return AudioQueueEnqueueBuffer(m_audio_queue, buffer, 0, nullptr) == noErr;
}

} // namespace RENDER
