#include "OpenCV-美颜/BeautyProcessor.h"
#include "engine/private/VceEngineImpl.h"
#include "engine/private/capture/CaptureManager.h"
#include "engine/private/render/IRenderManager.h"
#include "engine/private/service/MockServiceManager.h"
#include "engine/private/transport/MockTransportManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

using namespace VCE;

namespace {
bool Check(bool ok, const char *message) {
  std::cout << (ok ? "PASS " : "FAIL ") << message << '\n';
  return ok;
}

std::shared_ptr<I420Frame> MakeFrame(int width = 64, int height = 48) {
  auto frame = std::make_shared<I420Frame>();
  frame->width = width;
  frame->height = height;
  frame->timestamp_us = 1234567;
  frame->user_id = "test-user";
  for (int p = 0; p < 3; ++p) {
    const size_t size = p == 0 ? frame->GetYPlaneSize() : frame->GetUVPlaneSize();
    frame->data[p] = std::shared_ptr<uint8_t[]>(new uint8_t[size]);
    std::memset(frame->data[p].get(), p == 1 ? 110 : 140, size);
  }
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      frame->data[0][y * width + x] =
          (x < width / 2 ? 80 : 180) + ((x + y) % 2 ? 6 : -6);
  return frame;
}

double Error(const I420Frame &frame) {
  double sum = 0;
  for (int y = 3; y < frame.height - 3; ++y)
    for (int x = 3; x < frame.width / 2 - 3; ++x)
      sum += std::abs(int(frame.data[0][y * frame.width + x]) - 80);
  return sum;
}

class RecordingRender final : public IRenderManager {
public:
  VideoFrame last;
  Result Init() override { return Result::kRet_Success; }
  Result Uninit() override { return Result::kRet_Success; }
  Result AddUser(const std::string &, bool) override { return Result::kRet_Success; }
  Result RemoveUser(const std::string &) override { return Result::kRet_Success; }
  Result RenderLocalVideoFrame(const VideoFrame &frame) override {
    last = frame;
    return Result::kRet_Success;
  }
  Result RenderRemoteVideoFrame(const VideoFrame &) override { return Result::kRet_Success; }
  Result PlayRemoteAudioFrame(const AudioFrame &) override { return Result::kRet_Success; }
  Result RenderUser(const std::string &) override { return Result::kRet_Success; }
  Result UpdateUserVideoSize(const std::string &, int, int) override { return Result::kRet_Success; }
  void SetPlaybackAudioCallback(PlaybackAudioCallback) override {}
};

class RecordingTransport final : public ITransportManager {
public:
  std::shared_ptr<I420Frame> last;

  Result Init(const TransportPublishConfig &config) override { return m_mock.Init(config); }
  Result Uninit() override { return m_mock.Uninit(); }
  Result SetRoomInfo(const TransportRoomInfo &info) override { return m_mock.SetRoomInfo(info); }
  TransportRoomInfo GetRoomInfo() const override { return m_mock.GetRoomInfo(); }
  Result StartPublishCameraVideo() override { return m_mock.StartPublishCameraVideo(); }
  Result StopPublishCameraVideo() override { return m_mock.StopPublishCameraVideo(); }
  Result StartPublishAudio() override { return m_mock.StartPublishAudio(); }
  Result StopPublishAudio() override { return m_mock.StopPublishAudio(); }
  Result PushVideoFrame(const std::shared_ptr<I420Frame> &frame) override {
    last = frame;
    return Result::kRet_Success;
  }
  Result PushAudioFrame(const std::shared_ptr<AudioFrame> &frame) override {
    return m_mock.PushAudioFrame(frame);
  }
  Result SubscribeUserAV(const std::string &id) override { return m_mock.SubscribeUserAV(id); }
  Result UnsubscribeUserAV(const std::string &id) override { return m_mock.UnsubscribeUserAV(id); }
  Result SubscribeUsersAV(const std::vector<std::string> &ids) override {
    return m_mock.SubscribeUsersAV(ids);
  }
  Result UnsubscribeUsersAV(const std::vector<std::string> &ids) override {
    return m_mock.UnsubscribeUsersAV(ids);
  }
  bool IsPublishingCameraVideo() const override { return m_mock.IsPublishingCameraVideo(); }
  bool IsPublishingAudio() const override { return m_mock.IsPublishingAudio(); }
  bool IsUserSubscribedAV(const std::string &id) const override {
    return m_mock.IsUserSubscribedAV(id);
  }
  std::vector<std::string> GetSubscribedUsers() const override {
    return m_mock.GetSubscribedUsers();
  }
  void SetTransportDataCallback(const std::shared_ptr<ITransportDataCallback> &callback) override {
    m_mock.SetTransportDataCallback(callback);
  }

private:
  MockTransportManager m_mock;
};
}

int main() {
  bool ok = true;
  BeautyProcessor processor;
  auto input = MakeFrame();
  const auto original_y = std::vector<uint8_t>(input->data[0].get(),
                                              input->data[0].get() + input->GetYPlaneSize());
  ok &= Check(processor.Process(input) == input, "disabled: original frame, no pixel copy");
  processor.SetEnabled(true);
  ok &= Check(!processor.Process(nullptr), "null frame bypass");
  auto invalid = std::make_shared<I420Frame>(*input);
  invalid->width = 63;
  ok &= Check(processor.Process(invalid) == invalid, "invalid dimensions bypass");
  auto output = processor.Process(input);
  ok &= Check(output != input && output->data[0] != input->data[0], "independent output luminance");
  ok &= Check(Error(*output) < Error(*input), "small luminance variations reduced");
  const int boundary = 20 * input->width + input->width / 2;
  ok &= Check(output->data[0][boundary] - output->data[0][boundary - 1] > 80,
              "strong edge retained");
  ok &= Check(std::equal(original_y.begin(), original_y.end(), input->data[0].get()),
              "capture pixels unchanged");
  ok &= Check(output->data[1] == input->data[1] && output->data[2] == input->data[2] &&
              output->timestamp_us == input->timestamp_us && output->user_id == input->user_id,
              "chroma and metadata preserved");
  auto resized = processor.Process(MakeFrame(32, 24));
  ok &= Check(resized->HasData() && resized->width == 32 && resized->height == 24,
              "resolution changes supported");

  auto render = std::make_unique<RecordingRender>();
  auto *render_ptr = render.get();
  auto transport = std::make_unique<RecordingTransport>();
  auto *transport_ptr = transport.get();
  auto engine = std::make_shared<VceEngineImpl>(
      std::make_unique<CaptureManager>(), std::move(render), std::move(transport),
      std::make_unique<MockServiceManager>());
  ok &= Check(engine->SetBeautyEnabled(true) == Result::kRet_NotInitialized,
              "engine rejects switch before initialization");
  ok &= Check(engine->Init() == Result::kRet_Success, "engine initializes");
  auto capture_callback = std::static_pointer_cast<ICaptureDataCallback>(engine);
  capture_callback->OnCaptureVideoFrame(input);
  ok &= Check(render_ptr->last.data[0] == input->data[0] &&
              transport_ptr->last && transport_ptr->last->data[0] == input->data[0],
              "default: preview and transport receive original pixels");
  ok &= Check(engine->SetBeautyEnabled(true) == Result::kRet_Success, "engine enables beauty");
  capture_callback->OnCaptureVideoFrame(input);
  ok &= Check(transport_ptr->last && render_ptr->last.data[0] == transport_ptr->last->data[0] &&
              render_ptr->last.data[0] != input->data[0] && Error(render_ptr->last) < Error(*input),
              "preview and transport share smoothed pixels");
  engine->SetBeautyEnabled(false);
  capture_callback->OnCaptureVideoFrame(input);
  ok &= Check(render_ptr->last.data[0] == input->data[0], "disable restores original");
  engine->SetBeautyEnabled(true);
  engine->Uninit();
  engine->Init();
  capture_callback->OnCaptureVideoFrame(input);
  ok &= Check(render_ptr->last.data[0] == input->data[0], "reinitialization resets switch");
  engine->Uninit();
  return ok ? 0 : 1;
}
