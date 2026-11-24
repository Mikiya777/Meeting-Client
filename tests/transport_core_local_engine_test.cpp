#include "transport/src/local/LocalTransportEngine.h"

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

std::shared_ptr<TRANSPORT::I420Frame> MakeVideoFrame(int64_t timestamp_us) {
  auto frame = std::make_shared<TRANSPORT::I420Frame>();
  frame->user_id = "local-user";
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

std::shared_ptr<TRANSPORT::AudioFrame> MakeAudioFrame(int64_t timestamp_us) {
  auto frame = std::make_shared<TRANSPORT::AudioFrame>();
  frame->user_id = "local-user";
  frame->sample_rate = 48000;
  frame->channels = 2;
  frame->samples = 480;
  frame->timestamp_us = timestamp_us;

  frame->data = std::shared_ptr<uint8_t[]>(new uint8_t[frame->DataSize()]);
  std::memset(frame->data.get(), 0, frame->DataSize());
  return frame;
}

} // namespace

int main() {
  bool passed = true;

  TRANSPORT::LocalTransportEngine engine;
  std::vector<TRANSPORT::ConnectionState> states;
  std::vector<std::string> remote_video_users;
  std::vector<std::shared_ptr<TRANSPORT::I420Frame>> remote_video_frames;
  std::vector<std::string> remote_audio_users;
  std::vector<std::shared_ptr<TRANSPORT::AudioFrame>> remote_audio_frames;

  engine.RegisterConnectionStateCallback(
      [&](TRANSPORT::ConnectionState state) {
        states.push_back(state);
      });
  engine.RegisterVideoCallback(
      [&](const std::string &user_id,
          const std::shared_ptr<TRANSPORT::I420Frame> &frame) {
        remote_video_users.push_back(user_id);
        remote_video_frames.push_back(frame);
      });
  engine.RegisterAudioCallback(
      [&](const std::string &user_id,
          const std::shared_ptr<TRANSPORT::AudioFrame> &frame) {
        remote_audio_users.push_back(user_id);
        remote_audio_frames.push_back(frame);
      });

  TRANSPORT::PublishInfo publish_info;
  publish_info.video_width = 640;
  publish_info.video_height = 360;
  publish_info.video_fps = 30;
  publish_info.audio_sample_rate = 48000;
  publish_info.audio_channels = 2;
  passed &= Check(engine.Initialize(publish_info),
                  "LocalTransportEngine initializes");

  TRANSPORT::TargetRoomInfo room_info;
  room_info.local_user_id = "local-user";
  room_info.room_id = "room-local";
  room_info.push_server_url = "local-push";
  room_info.pull_server_url = "local-pull";
  engine.SetTargetRoomInfo(room_info);
  passed &= Check(engine.GetTargetRoomInfo().room_id == "room-local",
                  "LocalTransportEngine stores room info");

  passed &= Check(!engine.PushVideoFrame(MakeVideoFrame(1'000'000)),
                  "video push is rejected before publishing");
  passed &= Check(engine.StartPublishVideo(),
                  "video publishing starts");
  passed &= Check(engine.StartPublishAudio(),
                  "audio publishing starts");
  passed &= Check(engine.IsPublishingVideo() && engine.IsPublishingAudio(),
                  "publishing flags are set");

  passed &= Check(engine.PushVideoFrame(MakeVideoFrame(2'000'000)),
                  "video push succeeds after publishing");
  passed &= Check(engine.PushAudioFrame(MakeAudioFrame(2'020'000)),
                  "audio push succeeds after publishing");
  passed &= Check(engine.GetPushedVideoFrameCount() == 1,
                  "video push count is recorded");
  passed &= Check(engine.GetPushedAudioFrameCount() == 1,
                  "audio push count is recorded");
  passed &= Check(engine.GetLastPushedVideoTimestampUs() == 2'000'000,
                  "video timestamp is preserved in lower engine");
  passed &= Check(engine.GetLastPushedAudioTimestampUs() == 2'020'000,
                  "audio timestamp is preserved in lower engine");

  passed &= Check(engine.SubscribeUserAV("remote-user"),
                  "remote user subscription succeeds");
  passed &= Check(engine.IsUserSubscribedAV("remote-user"),
                  "remote user subscription is queryable");

  engine.InjectRemoteVideoFrame("remote-user", MakeVideoFrame(3'000'000));
  engine.InjectRemoteAudioFrame("remote-user", MakeAudioFrame(3'010'000));
  passed &= Check(remote_video_users.size() == 1 &&
                      remote_video_users.back() == "remote-user",
                  "remote video reaches lower callback");
  passed &= Check(remote_audio_users.size() == 1 &&
                      remote_audio_users.back() == "remote-user",
                  "remote audio reaches lower callback");
  passed &= Check(remote_video_frames.back()->timestamp_us == 3'000'000,
                  "remote video timestamp is preserved by lower callback");
  passed &= Check(remote_audio_frames.back()->timestamp_us == 3'010'000,
                  "remote audio timestamp is preserved by lower callback");

  passed &= Check(engine.UnsubscribeUserAV("remote-user"),
                  "remote user unsubscription succeeds");
  passed &= Check(!engine.IsUserSubscribedAV("remote-user"),
                  "remote user is no longer subscribed");

  engine.Uninit();
  passed &= Check(!states.empty() &&
                      states.back() == TRANSPORT::ConnectionState::kClosed,
                  "lower engine reports closed state");

  return passed ? 0 : 1;
}
