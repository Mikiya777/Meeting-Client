#include "transport/src/codec/SrsMediaCodec.h"

#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

namespace {

constexpr int kWidth = 160;
constexpr int kHeight = 96;

bool Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

bool MakeSequence(std::vector<std::vector<uint8_t>> &packets) {
  ISVCEncoder *raw_encoder = nullptr;
  if (WelsCreateSVCEncoder(&raw_encoder) != 0 || !raw_encoder) {
    return false;
  }
  const auto destroy = [](ISVCEncoder *encoder) {
    encoder->Uninitialize();
    WelsDestroySVCEncoder(encoder);
  };
  std::unique_ptr<ISVCEncoder, decltype(destroy)> encoder(raw_encoder, destroy);
  SEncParamExt config{};
  if (encoder->GetDefaultParams(&config) != 0) {
    return false;
  }
  config.iUsageType = CAMERA_VIDEO_REAL_TIME;
  config.iPicWidth = kWidth;
  config.iPicHeight = kHeight;
  config.fMaxFrameRate = 30;
  config.iRCMode = RC_OFF_MODE;
  config.iTargetBitrate = 350000;
  config.bEnableFrameSkip = false;
  config.iSpatialLayerNum = 1;
  config.iMultipleThreadIdc = 1;
  auto &layer = config.sSpatialLayers[0];
  layer.iVideoWidth = kWidth;
  layer.iVideoHeight = kHeight;
  layer.fFrameRate = 30;
  layer.iSpatialBitrate = 350000;
  layer.sSliceArgument.uiSliceMode = SM_FIXEDSLCNUM_SLICE;
  layer.sSliceArgument.uiSliceNum = 2;
  if (encoder->InitializeExt(&config) != 0) {
    return false;
  }

  std::vector<uint8_t> y(kWidth * kHeight);
  std::vector<uint8_t> u(kWidth * kHeight / 4, 128);
  std::vector<uint8_t> v(kWidth * kHeight / 4, 128);
  SSourcePicture picture{};
  picture.iColorFormat = videoFormatI420;
  picture.iPicWidth = kWidth;
  picture.iPicHeight = kHeight;
  picture.iStride[0] = kWidth;
  picture.iStride[1] = picture.iStride[2] = kWidth / 2;
  picture.pData[0] = y.data();
  picture.pData[1] = u.data();
  picture.pData[2] = v.data();

  for (int frame = 0; frame < 5; ++frame) {
    for (int row = 0; row < kHeight; ++row) {
      std::memset(y.data() + row * kWidth, 40 + row + frame * 8, kWidth);
    }
    if (frame == 3 && encoder->ForceIntraFrame(true) != 0) {
      return false;
    }
    picture.uiTimeStamp = frame * 33;
    SFrameBSInfo encoded{};
    if (encoder->EncodeFrame(&picture, &encoded) != 0 ||
        encoded.eFrameType != (frame == 0 || frame == 3 ? videoFrameTypeIDR
                                                       : videoFrameTypeP)) {
      return false;
    }

    auto &packet = packets.emplace_back();
    int slices = 0;
    for (int i = 0; i < encoded.iLayerNum; ++i) {
      const auto &encoded_layer = encoded.sLayerInfo[i];
      const auto *nal = encoded_layer.pBsBuf;
      for (int n = 0; n < encoded_layer.iNalCount; ++n) {
        const int length = encoded_layer.pNalLengthInByte[n];
        if (length < 5) {
          return false;
        }
        const int type = nal[nal[2] == 1 ? 3 : 4] & 0x1f;
        if (type == 1 || type == 5) {
          ++slices;
        }
        // Simulate one missing lower slice while leaving the first intact.
        if (!(frame == 1 && type == 1 && slices == 2)) {
          packet.insert(packet.end(), nal, nal + length);
        }
        nal += length;
      }
    }
    if (slices != 2) {
      return false;
    }
  }
  return true;
}

} // namespace

int main() {
  std::vector<std::vector<uint8_t>> packets;
  TRANSPORT::H264Decoder decoder;
  if (!Check(MakeSequence(packets), "encode five pictures with two slices each") ||
      !Check(decoder.Open(), "open the H.264 decoder")) {
    return 1;
  }

  std::shared_ptr<TRANSPORT::I420Frame> frame;
  const auto decode = [&](size_t index) {
    const auto &packet = packets[index];
    return decoder.Decode(packet.data(), packet.size(), static_cast<int64_t>(index), &frame);
  };

  bool passed = Check(decode(0) && frame && frame->IsValid(), "show the first clean IDR");
  const auto last_good = frame;
  passed &= Check(!decode(1) && frame == last_good,
                  "an incomplete picture does not replace the last clean frame");
  passed &= Check(!decode(2) && frame == last_good && decoder.NeedsKeyframe(),
                  "drop the concealed output and wait for an IDR");
  passed &= Check(decode(3) && frame && frame->IsValid() &&
                      frame->timestamp_us == 3 && !decoder.NeedsKeyframe(),
                  "resume on a clean IDR");
  passed &= Check(decode(4) && frame && frame->timestamp_us == 4,
                  "show predicted pictures again after recovery");
  return passed ? 0 : 1;
}
