#include "engine/private/service/GrpcServiceManager.h"
#include "transport/src/srs/SrsTransportEngine.h"

#include <chrono>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {

bool Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "[FAILED] " << message << '\n';
    return false;
  }

  std::cout << "[PASSED] " << message << '\n';
  return true;
}

std::string MakeUniqueUsername() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  return "client-grpc-" + std::to_string(millis);
}

bool EnvFlagEnabled(const char *name) {
  const char *value = std::getenv(name);
  if (value == nullptr) {
    return false;
  }

  const std::string flag(value);
  return flag == "1" || flag == "true" || flag == "TRUE" ||
         flag == "on" || flag == "ON" || flag == "yes" || flag == "YES";
}

std::shared_ptr<TRANSPORT::I420Frame> MakeVideoFrame(
    int width,
    int height,
    int64_t timestamp_us) {
  auto frame = std::make_shared<TRANSPORT::I420Frame>();
  frame->width = width;
  frame->height = height;
  frame->timestamp_us = timestamp_us;

  const size_t y_size = frame->GetYPlaneSize();
  const size_t uv_size = frame->GetUVPlaneSize();
  frame->data[0] = std::shared_ptr<uint8_t[]>(new uint8_t[y_size]);
  frame->data[1] = std::shared_ptr<uint8_t[]>(new uint8_t[uv_size]);
  frame->data[2] = std::shared_ptr<uint8_t[]>(new uint8_t[uv_size]);
  std::memset(frame->data[0].get(), 80, y_size);
  std::memset(frame->data[1].get(), 128, uv_size);
  std::memset(frame->data[2].get(), 128, uv_size);
  return frame;
}

std::shared_ptr<TRANSPORT::AudioFrame> MakeAudioFrame(
    int sample_rate,
    int channels,
    int64_t timestamp_us) {
  auto frame = std::make_shared<TRANSPORT::AudioFrame>();
  frame->sample_rate = sample_rate;
  frame->channels = channels;
  frame->samples = 960;
  frame->timestamp_us = timestamp_us;
  frame->data = std::shared_ptr<uint8_t[]>(new uint8_t[frame->DataSize()]);
  std::memset(frame->data.get(), 0, frame->DataSize());
  return frame;
}

} // namespace

int main(int argc, char **argv) {
  const std::string server_address =
      argc > 1 ? argv[1] : "127.0.0.1:50051";
  VCE::GrpcServiceManager service_manager(server_address);

  bool passed = true;
  passed &= Check(service_manager.Init() == VCE::Result::kRet_Success,
                  "grpc service manager initializes");

  const std::string username = MakeUniqueUsername();
  const std::string password = "demo-password";

  VCE::ServiceUserInfo registered_user;
  passed &= Check(service_manager.RegisterUser(username, password, 0,
                                               &registered_user) ==
                      VCE::Result::kRet_Success,
                  "client registers through server");
  passed &= Check(!registered_user.user_id.empty(),
                  "server returns a user id");

  VCE::ServiceUserInfo logged_in_user;
  passed &= Check(service_manager.Login(username, password, 0,
                                        &logged_in_user) ==
                      VCE::Result::kRet_Success,
                  "client logs in through server");
  passed &= Check(logged_in_user.user_id == registered_user.user_id,
                  "login returns the same user id");

  VCE::MeetingInfo meeting;
  passed &= Check(service_manager.CreateMeeting(
                      logged_in_user, "Client gRPC smoke",
                      "client-server-srs route smoke test", 0, &meeting) ==
                      VCE::Result::kRet_Success,
                  "client creates meeting through server");
  passed &= Check(!meeting.meeting_id.empty(),
                  "server returns a meeting id");
  passed &= Check(meeting.active, "server returns an active meeting");
  passed &= Check(!meeting.media_route.push_server_url.empty(),
                  "server returns SRS push URL");
  passed &= Check(!meeting.media_route.pull_server_url.empty(),
                  "server returns SRS pull URL");
  passed &= Check(!meeting.media_route.srs_app.empty(),
                  "server returns SRS app");

  std::vector<VCE::MeetingInfo> meetings;
  passed &= Check(service_manager.ListMeetings(true, &meetings) ==
                      VCE::Result::kRet_Success,
                  "client lists active meetings through server");
  passed &= Check(!meetings.empty(),
                  "client receives active meeting list");

  TRANSPORT::SrsTransportEngine transport_engine;
  std::atomic<int> received_video_frames{0};
  std::atomic<int> received_audio_frames{0};
  transport_engine.RegisterVideoCallback(
      [&](const std::string &user_id,
          const std::shared_ptr<TRANSPORT::I420Frame> &frame) {
        if (user_id == logged_in_user.user_id && frame && frame->IsValid()) {
          ++received_video_frames;
        }
      });
  transport_engine.RegisterAudioCallback(
      [&](const std::string &user_id,
          const std::shared_ptr<TRANSPORT::AudioFrame> &frame) {
        if (user_id == logged_in_user.user_id && frame && frame->IsValid()) {
          ++received_audio_frames;
        }
      });

  TRANSPORT::PublishInfo publish_info;
  publish_info.video_width = 1280;
  publish_info.video_height = 720;
  publish_info.video_fps = 30;
  publish_info.audio_sample_rate = 48000;
  publish_info.audio_channels = 2;
  passed &= Check(transport_engine.Initialize(publish_info),
                  "SRS transport initializes");

  TRANSPORT::TargetRoomInfo room_info;
  room_info.local_user_id = logged_in_user.user_id;
  room_info.room_id = meeting.meeting_id;
  room_info.push_server_url = meeting.media_route.push_server_url;
  room_info.pull_server_url = meeting.media_route.pull_server_url;
  room_info.srs_app = meeting.media_route.srs_app;
  room_info.srs_secret = meeting.media_route.srs_secret;
  transport_engine.SetTargetRoomInfo(room_info);

  passed &= Check(transport_engine.StartPublishVideo(),
                  "SRS transport starts publish session");
  passed &= Check(transport_engine.StartPublishAudio(),
                  "SRS transport starts audio publish");
  passed &= Check(transport_engine.GetPublishEndpoint().find("/rtc/v1/whip/") !=
                      std::string::npos,
                  "SRS transport builds WHIP endpoint");
  passed &= Check(transport_engine.SubscribeUserAV(logged_in_user.user_id),
                  "SRS transport starts pull session");
  passed &= Check(transport_engine.GetPullEndpoint(logged_in_user.user_id)
                          .find("/rtc/v1/whep/") != std::string::npos,
                  "SRS transport builds WHEP endpoint");

  if (EnvFlagEnabled("MEETING_SRS_EXPECT_MEDIA")) {
    const int64_t base_timestamp_us = 1'000'000;
    for (int index = 0; index < 180; ++index) {
      const int64_t timestamp_us = base_timestamp_us + index * 33'333;
      transport_engine.PushVideoFrame(
          MakeVideoFrame(publish_info.video_width,
                         publish_info.video_height,
                         timestamp_us));
      transport_engine.PushAudioFrame(
          MakeAudioFrame(publish_info.audio_sample_rate,
                         publish_info.audio_channels,
                         base_timestamp_us + index * 20'000));
      if (received_video_frames.load() > 0 &&
          received_audio_frames.load() > 0) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    passed &= Check(received_video_frames.load() > 0,
                    "SRS WHEP receives and decodes video frame");
    passed &= Check(received_audio_frames.load() > 0,
                    "SRS WHEP receives and decodes audio frame");
  }

  transport_engine.UnsubscribeUserAV(logged_in_user.user_id);
  transport_engine.StopPublishAudio();
  transport_engine.StopPublishVideo();
  transport_engine.Uninit();

  std::vector<VCE::ServiceUserInfo> participants;
  passed &= Check(service_manager.EndMeeting(logged_in_user,
                                             meeting.meeting_id,
                                             &participants) ==
                      VCE::Result::kRet_Success,
                  "client ends meeting through server");

  service_manager.Uninit();
  return passed ? 0 : 1;
}
