#ifndef VCE_TYPES_H
#define VCE_TYPES_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace VCE {

enum class Result {
  kRet_Success = 0,
  kRet_AlreadyInitialized,
  kRet_NotInitialized,
  kRet_InvalidArgument,
  kRet_InternalError,
  kRet_InvalidState
};

enum class EngineState {
  kES_Created = 0,
  kES_Initialized,
  kES_ShuttingDown,
  kES_Stopped
};

enum class TransportState {
  kDisconnected = 0,
  kConnecting,
  kConnected,
  kDisconnecting,
  kFailed,
  kClosed
};

struct TransportPublishConfig {
  int video_width = 640;
  int video_height = 480;
  int video_fps = 24;
  int audio_sample_rate = 48000;
  int audio_channels = 2;
};

struct TransportRoomInfo {
  std::string local_user_id;
  std::string room_id;
  std::string push_server_url;
  std::string pull_server_url;
  std::string srs_app = "meeting";
  std::string srs_secret;
};

struct ServiceUserInfo {
  std::string user_id;
  std::string username;
  uint32_t client_ip = 0;
};

struct MediaDeviceInfo {
  std::string device_id;
  std::string device_name;
  bool selected = false;
};

struct MeetingMediaRoute {
  std::string push_server_url;
  std::string pull_server_url;
  std::string srs_app = "meeting";
  std::string srs_secret;
  std::string stream_prefix;
};

struct MeetingInfo {
  std::string meeting_id;
  std::string title;
  std::string description;
  ServiceUserInfo creator;
  std::vector<ServiceUserInfo> participants;
  bool active = false;
  MeetingMediaRoute media_route;
};

struct I420Frame {
  std::shared_ptr<uint8_t[]> data[3];
  std::string user_id;
  int width = 0;
  int height = 0;
  int64_t timestamp_us = 0;

  size_t GetYPlaneSize() const {
    return static_cast<size_t>(width) * static_cast<size_t>(height);
  }

  size_t GetUVPlaneSize() const {
    return static_cast<size_t>(width / 2) * static_cast<size_t>(height / 2);
  }

  size_t GetDataSize() const {
    return GetYPlaneSize() + GetUVPlaneSize() * 2;
  }

  bool HasData() const {
    return data[0] && data[1] && data[2] &&
           width > 0 && height > 0 &&
           width % 2 == 0 && height % 2 == 0;
  }
};

using VideoFrame = I420Frame;

struct AudioFrame {
  std::shared_ptr<uint8_t[]> data;
  std::string user_id;
  int sample_rate = 0;
  int channels = 0;
  int samples = 0;
  int64_t timestamp_us = 0;

  size_t DataSize() const {
    return static_cast<size_t>(samples) *
           static_cast<size_t>(channels) *
           sizeof(float);
  }

  bool HasData() const {
    return data && sample_rate > 0 && channels > 0 && samples > 0;
  }
};

} // namespace VCE

#endif // VCE_TYPES_H
