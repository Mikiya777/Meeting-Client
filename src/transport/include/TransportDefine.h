#ifndef TRANSPORT_DEFINE_H
#define TRANSPORT_DEFINE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace TRANSPORT {

//连接状态
enum class ConnectionState {
  kDisconnected = 0,
  kConnecting,
  kConnected,
  kDisconnecting,
  kFailed,
  kClosed
};

//本次发布会话的音视频规格信息
struct PublishInfo {
  int video_width = 0;
  int video_height = 0;
  int video_fps = 0;
  int audio_sample_rate = 0;
  int audio_channels = 0;
};

//房间信息
struct TargetRoomInfo {
  std::string local_user_id;
  std::string room_id;
  std::string push_server_url;
  std::string pull_server_url;
  std::string srs_app = "meeting";
  std::string srs_secret;
};

//视频帧信息
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

  bool IsValid() const {
    return data[0] && data[1] && data[2] &&
           width > 0 && height > 0 &&
           width % 2 == 0 && height % 2 == 0;
  }
};

struct AudioFrame {
  std::shared_ptr<uint8_t[]> data;
  std::string user_id;
  int samples = 0;
  int channels = 0;
  int sample_rate = 0;
  int64_t timestamp_us = 0;

  size_t DataSize() const {
    return static_cast<size_t>(samples) *
           static_cast<size_t>(channels) *
           sizeof(float);
  }

  bool IsValid() const {
    return data && samples > 0 && channels > 0 && sample_rate > 0;
  }
};

} // namespace TRANSPORT

#endif // TRANSPORT_DEFINE_H
