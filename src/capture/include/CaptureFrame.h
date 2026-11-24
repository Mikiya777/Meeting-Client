#ifndef CAPTURE_FRAME_H
#define CAPTURE_FRAME_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include "capture/include/CaptureDefine.h"

/*
组件位置：
OBS 原始帧
   ↓ 转换
CaptureFrame.h 里定义的统一帧格式
   ↓ 回调给外部
CaptureManager / UI / 编码 / 推流 / 会议模块

功能：
1.负责规定好采集模块对外输出的数据格式。
2.数据零拷贝跨模块传输
*/
namespace CAPTURE {
//统一数据格式
struct I420Frame {
  std::shared_ptr<uint8_t[]> data[3]; //shared_ptr :支持数据零拷贝跨模块传递，YUV每一个分量通常是一字节，用uint8_t最合适
  int width = 0;
  int height = 0;
  int64_t timestamp_us = 0; //时间戳， 一般很长因此用int64_t存储

  //这几个函数的数据源是结构体，因此不需要参数，返回值是size_t是为了表示“内存大小”
  size_t GetYPlaneSize() const {
    return static_cast<size_t>(width) * static_cast<size_t>(height);
  }

  size_t GetUVPlaneSize() const {
    return static_cast<size_t>(width / 2) * static_cast<size_t>(height / 2);
  }

  size_t GetDataSize() const {
    return GetYPlaneSize() + GetUVPlaneSize() * 2;
  }

  // 合法性检验，根据结构体内部的数据进行检验，不需要参数；I420Frame 宽高必须是正偶数
  CaptureResult Validate() const {
    if (!data[0] || !data[1] || !data[2]) {
      return CaptureResult::kInvalidData;
    }

    if (width <= 0 || height <= 0) {
      return CaptureResult::kInvalidSize;
    }

    if (width % 2 != 0 || height % 2 != 0) {
      return CaptureResult::kInvalidFormat;
    }

    return CaptureResult::kSuccess;
  }
};

struct AudioFrame {
  std::shared_ptr<uint8_t[]> data; //同Videoframe

  int samples = 0; //采样点数
  int channels = 0; //通道数
  int sample_rate = 0; //采样率
  int64_t timestamp_us = 0; //时间戳

  size_t GetDataSize() const {
    // 4字节，音频数据里，每一个采样点是 float 类型，所以每个采样点占 sizeof(float) 字节，且写法更安全
    return static_cast<size_t>(samples) * static_cast<size_t>(channels) * sizeof(float);
  }

  CaptureResult Validate() const {
    if (!data) {
      return CaptureResult::kInvalidData;
    }

    if (samples <= 0 || channels <= 0 || sample_rate <= 0) {
      return CaptureResult::kInvalidArgument;
    }

    return CaptureResult::kSuccess;
  }
};

} // namespace CAPTURE

#endif //CAPTURE_FRAME_H

/*
笔记：
  1.涉及内存大小时，返回值常用 size_t（真实大小在编译期由具体硬件平台决定）；
  2.涉及原始字节数据时，存储类型常用 uint8_t（1字节）
  3.涉及每个样本是什么格式时，要用真实语义类型 sizeof（type）
*/
