#include "engine/private/transport/ITransportDataCallback.h"
#include "engine/private/transport/TransportManager.h"

#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
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

std::shared_ptr<VCE::I420Frame> MakeVideoFrame(int64_t timestamp_us) {
  auto frame = std::make_shared<VCE::I420Frame>();
  frame->user_id = "local-user";
  frame->width = 640;
  frame->height = 360;
  frame->timestamp_us = timestamp_us;

  const size_t y_size = frame->GetYPlaneSize();
  const size_t uv_size = frame->GetUVPlaneSize();
  frame->data[0] = std::shared_ptr<uint8_t[]>(new uint8_t[y_size]);
  frame->data[1] = std::shared_ptr<uint8_t[]>(new uint8_t[uv_size]);
  frame->data[2] = std::shared_ptr<uint8_t[]>(new uint8_t[uv_size]);

  std::memset(frame->data[0].get(), 90, y_size);
  std::memset(frame->data[1].get(), 128, uv_size);
  std::memset(frame->data[2].get(), 128, uv_size);
  return frame;
}

std::shared_ptr<VCE::AudioFrame> MakeAudioFrame(int64_t timestamp_us) {
  auto frame = std::make_shared<VCE::AudioFrame>();
  frame->user_id = "local-user";
  frame->sample_rate = 48000;
  frame->channels = 2;
  frame->samples = 480;
  frame->timestamp_us = timestamp_us;

  frame->data = std::shared_ptr<uint8_t[]>(new uint8_t[frame->DataSize()]);
  std::memset(frame->data.get(), 0, frame->DataSize());
  return frame;
}

class CallbackSpy final : public VCE::ITransportDataCallback {
public:
  void OnTransportVideoFrame(const std::string &user_id,
                             const std::shared_ptr<VCE::I420Frame> &frame) override {
    remote_video_users.push_back(user_id);
    remote_video_frames.push_back(frame);
  }

  void OnTransportAudioFrame(const std::string &user_id,
                             const std::shared_ptr<VCE::AudioFrame> &frame) override {
    remote_audio_users.push_back(user_id);
    remote_audio_frames.push_back(frame);
  }

  void OnTransportConnectionStateChanged(VCE::TransportState state) override {
    states.push_back(state);
  }

  std::vector<std::string> remote_video_users;
  std::vector<std::shared_ptr<VCE::I420Frame>> remote_video_frames;
  std::vector<std::string> remote_audio_users;
  std::vector<std::shared_ptr<VCE::AudioFrame>> remote_audio_frames;
  std::vector<VCE::TransportState> states;
};

bool WaitUntilSent(VCE::TransportManager &manager,
                   int video_count,
                   int audio_count) {
  for (int i = 0; i < 100; ++i) {
    if (manager.GetSentVideoFrameCount() >= video_count &&
        manager.GetSentAudioFrameCount() >= audio_count) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

bool CheckShutdownDuringSubmission(const VCE::TransportPublishConfig &config) {
  using VCE::Result;
  VCE::TransportManager manager;

  // Exercise the idle wait/stop handoff repeatedly on the same manager.
  for (int i = 0; i < 100; ++i) {
    if (manager.Init(config) != Result::kRet_Success ||
        manager.Uninit() != Result::kRet_Success) {
      return false;
    }
  }

  if (manager.Init(config) != Result::kRet_Success ||
      manager.StartPublishCameraVideo() != Result::kRet_Success ||
      manager.StartPublishAudio() != Result::kRet_Success) {
    return false;
  }

  const auto video = MakeVideoFrame(4'000'000);
  const auto audio = MakeAudioFrame(4'010'000);
  std::promise<bool> first_submission;
  auto producer_started = first_submission.get_future();
  Result producer_result = Result::kRet_Success;
  std::thread producer([&] {
    const auto push_pair = [&] {
      const auto video_result = manager.PushVideoFrame(video);
      return video_result == Result::kRet_Success
                 ? manager.PushAudioFrame(audio)
                 : video_result;
    };
    producer_result = push_pair();
    first_submission.set_value(producer_result == Result::kRet_Success);
    while (producer_result == Result::kRet_Success) {
      producer_result = push_pair();
    }
  });

  const bool submitted = producer_started.get();
  const auto stopped = manager.Uninit();
  producer.join();
  return submitted && stopped == Result::kRet_Success &&
         producer_result == Result::kRet_NotInitialized &&
         manager.GetQueuedVideoFrameCount() == 0 &&
         manager.GetQueuedAudioFrameCount() == 0;
}

} // namespace

int main() {
  bool passed = true;

  VCE::TransportManager manager;
  auto callback = std::make_shared<CallbackSpy>();
  manager.SetTransportDataCallback(callback);

  VCE::TransportPublishConfig config;
  config.video_width = 640;
  config.video_height = 360;
  config.video_fps = 30;
  config.audio_sample_rate = 48000;
  config.audio_channels = 2;

  passed &= Check(manager.Init(config) == VCE::Result::kRet_Success,
                  "real TransportManager initializes");

  VCE::TransportRoomInfo room_info;
  room_info.local_user_id = "local-user";
  room_info.room_id = "room-queue";
  passed &= Check(manager.SetRoomInfo(room_info) == VCE::Result::kRet_Success,
                  "real TransportManager accepts room info");

  passed &= Check(manager.PushVideoFrame(MakeVideoFrame(1'000'000)) ==
                      VCE::Result::kRet_InvalidState,
                  "video push is rejected before publishing starts");
  passed &= Check(manager.PushAudioFrame(MakeAudioFrame(1'000'000)) ==
                      VCE::Result::kRet_InvalidState,
                  "audio push is rejected before publishing starts");

  passed &= Check(manager.StartPublishCameraVideo() == VCE::Result::kRet_Success,
                  "video publishing starts on real manager");
  passed &= Check(manager.StartPublishAudio() == VCE::Result::kRet_Success,
                  "audio publishing starts on real manager");
  passed &= Check(manager.IsPublishingCameraVideo(),
                  "video publishing flag is set");
  passed &= Check(manager.IsPublishingAudio(),
                  "audio publishing flag is set");

  const int64_t video_ts = 2'000'000;
  const int64_t audio_ts = 2'020'000;
  passed &= Check(manager.PushVideoFrame(MakeVideoFrame(video_ts)) == VCE::Result::kRet_Success,
                  "video frame enters send queue");
  passed &= Check(manager.PushAudioFrame(MakeAudioFrame(audio_ts)) == VCE::Result::kRet_Success,
                  "audio frame enters send queue");

  passed &= Check(WaitUntilSent(manager, 1, 1),
                  "consumer threads send queued audio and video");
  passed &= Check(manager.GetLastSentVideoTimestampUs() == video_ts,
                  "video timestamp is preserved through send queue");
  passed &= Check(manager.GetLastSentAudioTimestampUs() == audio_ts,
                  "audio timestamp is preserved through send queue");

  passed &= Check(manager.StopPublishCameraVideo() == VCE::Result::kRet_Success &&
                      manager.GetQueuedVideoFrameCount() == 0 &&
                      manager.IsPublishingAudio(),
                  "video stop clears its queue and leaves audio active");
  passed &= Check(manager.PushVideoFrame(MakeVideoFrame(video_ts + 1)) ==
                      VCE::Result::kRet_InvalidState,
                  "video stop rejects further submissions");
  passed &= Check(manager.StopPublishAudio() == VCE::Result::kRet_Success &&
                      manager.GetQueuedAudioFrameCount() == 0,
                  "audio stop clears its queue");
  passed &= Check(manager.PushAudioFrame(MakeAudioFrame(audio_ts + 1)) ==
                      VCE::Result::kRet_InvalidState,
                  "audio stop rejects further submissions");
  passed &= Check(manager.StartPublishCameraVideo() == VCE::Result::kRet_Success &&
                      manager.StartPublishAudio() == VCE::Result::kRet_Success &&
                      manager.PushVideoFrame(MakeVideoFrame(video_ts + 2)) ==
                          VCE::Result::kRet_Success &&
                      manager.PushAudioFrame(MakeAudioFrame(audio_ts + 2)) ==
                          VCE::Result::kRet_Success &&
                      WaitUntilSent(manager, 2, 2) &&
                      manager.GetLastSentVideoTimestampUs() == video_ts + 2 &&
                      manager.GetLastSentAudioTimestampUs() == audio_ts + 2,
                  "restarted streams send fresh audio and video");

  passed &= Check(manager.SubscribeUserAV("remote-user") == VCE::Result::kRet_Success,
                  "real manager subscribes remote user");
  passed &= Check(manager.IsUserSubscribedAV("remote-user"),
                  "real manager reports subscribed remote user");

  auto remote_video = MakeVideoFrame(3'000'000);
  auto remote_audio = MakeAudioFrame(3'010'000);
  manager.InjectRemoteVideoFrame("remote-user", remote_video);
  manager.InjectRemoteAudioFrame("remote-user", remote_audio);

  passed &= Check(callback->remote_video_users.size() == 1 &&
                      callback->remote_video_users.back() == "remote-user",
                  "remote video injection reaches callback");
  passed &= Check(callback->remote_audio_users.size() == 1 &&
                      callback->remote_audio_users.back() == "remote-user",
                  "remote audio injection reaches callback");
  passed &= Check(callback->remote_video_frames.back()->timestamp_us == 3'000'000,
                  "remote video timestamp is preserved");
  passed &= Check(callback->remote_audio_frames.back()->timestamp_us == 3'010'000,
                  "remote audio timestamp is preserved");

  passed &= Check(manager.UnsubscribeUserAV("remote-user") == VCE::Result::kRet_Success,
                  "real manager unsubscribes remote user");
  passed &= Check(!manager.IsUserSubscribedAV("remote-user"),
                  "remote user is no longer subscribed");

  passed &= Check(manager.Uninit() == VCE::Result::kRet_Success,
                  "real manager uninitializes");
  passed &= Check(!callback->states.empty() &&
                      callback->states.back() == VCE::TransportState::kClosed,
                  "real manager reports closed state on uninit");

  passed &= Check(CheckShutdownDuringSubmission(config),
                  "idle shutdown can repeat and active shutdown rejects late submissions");

  return passed ? 0 : 1;
}
