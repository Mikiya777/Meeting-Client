#include "engine/private/transport/ITransportDataCallback.h"
#include "engine/private/transport/MockTransportManager.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

bool Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "[FAILED] " << message << '\n';
    return false;
  }

  std::cout << "[PASSED] " << message << '\n';
  return true;
}

std::shared_ptr<VCE::I420Frame> MakeVideoFrame(const std::string &user_id,
                                               int64_t timestamp_us) {
  auto frame = std::make_shared<VCE::I420Frame>();
  frame->user_id = user_id;
  frame->width = 640;
  frame->height = 360;
  frame->timestamp_us = timestamp_us;

  const size_t y_size = frame->GetYPlaneSize();
  const size_t uv_size = frame->GetUVPlaneSize();
  frame->data[0] = std::shared_ptr<uint8_t[]>(new uint8_t[y_size]);
  frame->data[1] = std::shared_ptr<uint8_t[]>(new uint8_t[uv_size]);
  frame->data[2] = std::shared_ptr<uint8_t[]>(new uint8_t[uv_size]);

  std::memset(frame->data[0].get(), 96, y_size);
  std::memset(frame->data[1].get(), 128, uv_size);
  std::memset(frame->data[2].get(), 128, uv_size);
  return frame;
}

std::shared_ptr<VCE::AudioFrame> MakeAudioFrame(const std::string &user_id,
                                                int64_t timestamp_us) {
  auto frame = std::make_shared<VCE::AudioFrame>();
  frame->user_id = user_id;
  frame->sample_rate = 48000;
  frame->channels = 2;
  frame->samples = 480;
  frame->timestamp_us = timestamp_us;

  frame->data = std::shared_ptr<uint8_t[]>(new uint8_t[frame->DataSize()]);
  std::memset(frame->data.get(), 0, frame->DataSize());
  return frame;
}

class TransportCallbackSpy final : public VCE::ITransportDataCallback {
public:
  void OnTransportVideoFrame(const std::string &user_id,
                             const std::shared_ptr<VCE::I420Frame> &frame) override {
    video_users.push_back(user_id);
    video_frames.push_back(frame);
  }

  void OnTransportAudioFrame(const std::string &user_id,
                             const std::shared_ptr<VCE::AudioFrame> &frame) override {
    audio_users.push_back(user_id);
    audio_frames.push_back(frame);
  }

  void OnTransportConnectionStateChanged(VCE::TransportState state) override {
    states.push_back(state);
  }

  std::vector<std::string> video_users;
  std::vector<std::shared_ptr<VCE::I420Frame>> video_frames;
  std::vector<std::string> audio_users;
  std::vector<std::shared_ptr<VCE::AudioFrame>> audio_frames;
  std::vector<VCE::TransportState> states;
};

} // namespace

int main() {
  bool passed = true;

  VCE::MockTransportManager manager;
  VCE::TransportPublishConfig config;
  config.video_width = 640;
  config.video_height = 360;
  config.video_fps = 30;
  config.audio_sample_rate = 48000;
  config.audio_channels = 2;

  passed &= Check(manager.Init(config) == VCE::Result::kRet_Success,
                  "transport manager initializes with publish config");

  VCE::TransportRoomInfo room_info;
  room_info.local_user_id = "local-user";
  room_info.room_id = "room-1";
  room_info.push_server_url = "127.0.0.1";
  room_info.pull_server_url = "127.0.0.1";
  passed &= Check(manager.SetRoomInfo(room_info) == VCE::Result::kRet_Success,
                  "room info is accepted after init");
  passed &= Check(manager.GetRoomInfo().room_id == "room-1",
                  "room info can be queried back");

  passed &= Check(manager.StartPublishCameraVideo() == VCE::Result::kRet_Success,
                  "camera video publishing starts");
  passed &= Check(manager.StartPublishAudio() == VCE::Result::kRet_Success,
                  "audio publishing starts");
  passed &= Check(manager.IsPublishingCameraVideo(),
                  "camera publishing flag is true");
  passed &= Check(manager.IsPublishingAudio(),
                  "audio publishing flag is true");

  auto local_video = MakeVideoFrame("local-user", 1'000'000);
  auto local_audio = MakeAudioFrame("local-user", 1'000'000);
  passed &= Check(manager.PushVideoFrame(local_video) == VCE::Result::kRet_Success,
                  "valid video frame is accepted for publish");
  passed &= Check(manager.PushAudioFrame(local_audio) == VCE::Result::kRet_Success,
                  "valid audio frame is accepted for publish");
  passed &= Check(manager.GetPushedVideoFrameCount() == 1,
                  "video push count is recorded");
  passed &= Check(manager.GetPushedAudioFrameCount() == 1,
                  "audio push count is recorded");

  passed &= Check(manager.SubscribeUserAV("remote-user") == VCE::Result::kRet_Success,
                  "remote user subscription succeeds");
  passed &= Check(manager.IsUserSubscribedAV("remote-user"),
                  "remote user subscription is queryable");

  auto subscribed_users = manager.GetSubscribedUsers();
  passed &= Check(std::find(subscribed_users.begin(),
                            subscribed_users.end(),
                            "remote-user") != subscribed_users.end(),
                  "subscribed users include remote user");

  auto callback = std::make_shared<TransportCallbackSpy>();
  manager.SetTransportDataCallback(callback);

  auto remote_video = MakeVideoFrame("remote-user", 2'500'000);
  auto remote_audio = MakeAudioFrame("remote-user", 2'520'000);
  manager.SimulateRemoteVideoFrame("remote-user", remote_video);
  manager.SimulateRemoteAudioFrame("remote-user", remote_audio);
  manager.SimulateConnectionStateChanged(VCE::TransportState::kConnected);

  passed &= Check(callback->video_users.size() == 1 &&
                      callback->video_users.back() == "remote-user",
                  "remote video callback preserves user id");
  passed &= Check(callback->audio_users.size() == 1 &&
                      callback->audio_users.back() == "remote-user",
                  "remote audio callback preserves user id");
  passed &= Check(callback->video_frames.back()->timestamp_us == 2'500'000,
                  "remote video timestamp is preserved for AV sync");
  passed &= Check(callback->audio_frames.back()->timestamp_us == 2'520'000,
                  "remote audio timestamp is preserved for AV sync");
  passed &= Check(callback->states.size() == 1 &&
                      callback->states.back() == VCE::TransportState::kConnected,
                  "connection state callback preserves transport state");

  passed &= Check(manager.UnsubscribeUserAV("remote-user") == VCE::Result::kRet_Success,
                  "remote user unsubscription succeeds");
  passed &= Check(!manager.IsUserSubscribedAV("remote-user"),
                  "remote user is no longer subscribed");

  passed &= Check(manager.Uninit() == VCE::Result::kRet_Success,
                  "transport manager uninitializes");

  return passed ? 0 : 1;
}
