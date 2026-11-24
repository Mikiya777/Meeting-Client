#ifndef RENDER_DEFINE_H
#define RENDER_DEFINE_H

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

//跨平台导出宏：如果以后这个 RenderEngine 在 Windows 上编译成 DLL，IRenderEngine 这类对外接口需要导出符号。
#ifndef RENDER_ENGINE_API
#define RENDER_ENGINE_API
#endif

namespace RENDER {

struct I420Frame {
  std::shared_ptr<uint8_t[]> data[3];
  int width = 0;
  int height = 0;
  int64_t timestamp_us = 0;

  size_t GetYPlaneSize() const;
  size_t GetUVPlaneSize() const;
  size_t GetDataSize() const;
  bool HasData() const;
};

struct AudioFrame {
  std::shared_ptr<uint8_t[]> data;
  int sample_rate = 0;
  int channels = 0;
  int samples_per_channel = 0;
  int64_t timestamp_us = 0;

  size_t DataSize() const;
  bool HasData() const;
};

//一个扬声器设备
struct AudioSpeaker {
  std::string device_id;
  std::string name;
  bool is_default = false;
};

//一段混好的音频
struct MixedAudioChunk {
  int64_t start_ntp_ts_us = 0;
  uint32_t frame_count = 0;
  std::vector<float> data;
};

//防御性编程时需要的各种结果情况
enum class RenderResult {
  kSuccess = 0,
  kAlreadyInitialized,
  kNotInitialized,
  kInvalidArgument,
  kInvalidData,
  kInvalidSize,
  kInvalidFormat,
  kInternalError,
  kNotFound
};

} // namespace RENDER

#endif // RENDER_DEFINE_H
