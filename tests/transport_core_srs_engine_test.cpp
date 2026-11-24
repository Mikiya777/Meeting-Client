#include "transport/src/srs/SrsTransportEngine.h"

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
  frame->user_id = "alice";
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
  frame->user_id = "alice";
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

  TRANSPORT::SrsTransportEngine engine;
  std::vector<TRANSPORT::ConnectionState> states;
  engine.RegisterConnectionStateCallback(
      [&](TRANSPORT::ConnectionState state) {
        states.push_back(state);
      });

  TRANSPORT::PublishInfo publish_info;
  publish_info.video_width = 640;
  publish_info.video_height = 360;
  publish_info.video_fps = 30;
  publish_info.audio_sample_rate = 48000;
  publish_info.audio_channels = 2;
  passed &= Check(engine.Initialize(publish_info),
                  "SrsTransportEngine initializes");

  TRANSPORT::TargetRoomInfo room_info;
  room_info.local_user_id = "alice";
  room_info.room_id = "room-001";
  room_info.push_server_url = "http://62.234.24.190:2022";
  room_info.pull_server_url = "http://62.234.24.190:2022/";
  room_info.srs_app = "live";
  room_info.srs_secret = "secret value";
  engine.SetTargetRoomInfo(room_info);

  passed &= Check(engine.GetLocalStreamName() == "room-001_alice",
                  "local stream uses room and user id");
  passed &= Check(engine.GetPublishEndpoint() ==
                      "http://62.234.24.190:2022/rtc/v1/whip/?app=live&stream=room-001_alice&secret=secret%20value",
                  "WHIP endpoint is built from SRS base url");
  passed &= Check(engine.GetPullEndpoint("bob") ==
                      "http://62.234.24.190:2022/rtc/v1/whep/?app=live&stream=room-001_bob",
                  "WHEP endpoint is built for remote user");

  passed &= Check(!engine.PushVideoFrame(MakeVideoFrame(1'000'000)),
                  "video push is rejected before publishing");
  passed &= Check(engine.StartPublishVideo(),
                  "SRS video publishing starts");
  passed &= Check(engine.StartPublishAudio(),
                  "SRS audio publishing starts");
  passed &= Check(engine.IsPublishingVideo() && engine.IsPublishingAudio(),
                  "SRS publishing flags are set");
  passed &= Check(engine.PushVideoFrame(MakeVideoFrame(2'000'000)),
                  "SRS video frame is accepted");
  passed &= Check(engine.PushAudioFrame(MakeAudioFrame(2'020'000)),
                  "SRS audio frame is accepted");
  passed &= Check(engine.GetPushedVideoFrameCount() == 1,
                  "SRS video push count is recorded");
  passed &= Check(engine.GetPushedAudioFrameCount() == 1,
                  "SRS audio push count is recorded");
  passed &= Check(engine.GetLastPushedVideoTimestampUs() == 2'000'000,
                  "SRS video timestamp is preserved");
  passed &= Check(engine.GetLastPushedAudioTimestampUs() == 2'020'000,
                  "SRS audio timestamp is preserved");

  passed &= Check(engine.SubscribeUserAV("bob"),
                  "SRS remote user subscription starts");
  passed &= Check(engine.IsUserSubscribedAV("bob"),
                  "SRS remote user subscription is queryable");
  passed &= Check(engine.UnsubscribeUserAV("bob"),
                  "SRS remote user unsubscription succeeds");
  passed &= Check(!engine.IsUserSubscribedAV("bob"),
                  "SRS remote user is no longer subscribed");

  engine.Uninit();
  passed &= Check(!states.empty() &&
                      states.back() == TRANSPORT::ConnectionState::kClosed,
                  "SRS engine reports closed state");

  return passed ? 0 : 1;
}
