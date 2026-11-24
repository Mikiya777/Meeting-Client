#include "capture/src/macos/ObsRuntime.h"
#include "capture/src/macos/MacCaptureDevice.h"

#include <obs.h>
#include <obs-properties.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <string>
#include <cstring>
#include <utility>
#include <vector>

#ifdef __APPLE__
#include <limits.h>
#include <mach-o/dyld.h>
#include <stdlib.h>
#endif

/*
功能：OBS 第三方封装层的实现部分，负责把外部接口请求转发到底层实现
1. 建立 OBS 运行环境
2. 管理音视频 Source
3. 把音视频原始帧交给上层
*/

namespace CAPTURE {
namespace {

namespace fs = std::filesystem;

speaker_layout ToSpeakerLayout(int channels) {
  switch (channels) {
    case 1:
      return SPEAKERS_MONO;
    case 2:
      return SPEAKERS_STEREO;
    default:
      return SPEAKERS_UNKNOWN;
  }
}

bool CopyPlane(const uint8_t *src,
               uint32_t src_stride,
               uint8_t *dst,
               int dst_stride,
               int width,
               int height) {
  if (src == nullptr || dst == nullptr || src_stride < static_cast<uint32_t>(width) ||
      dst_stride < width || width <= 0 || height <= 0) {
    return false;
  }

  for (int row = 0; row < height; ++row) {
    std::memcpy(dst + row * dst_stride,
                src + row * src_stride,
                static_cast<size_t>(width));
  }

  return true;
}

uint8_t ClampByte(int value) {
  return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

const char *CameraPresetForSize(int width, int height) {
  if (width <= 640 && height <= 480) {
    return "AVCaptureSessionPreset640x480";
  }
  return "AVCaptureSessionPreset1280x720";
}

std::shared_ptr<I420Frame> ConvertBgraToI420(const video_data *frame,
                                             int width,
                                             int height) {
  if (frame == nullptr || width <= 0 || height <= 0 ||
      width % 2 != 0 || height % 2 != 0 ||
      frame->data[0] == nullptr ||
      frame->linesize[0] < static_cast<uint32_t>(width * 4)) {
    return nullptr;
  }

  auto output_frame = std::make_shared<I420Frame>();
  output_frame->width = width;
  output_frame->height = height;
  output_frame->timestamp_us = static_cast<int64_t>(frame->timestamp / 1000);

  const size_t y_size = output_frame->GetYPlaneSize();
  const size_t uv_size = output_frame->GetUVPlaneSize();
  output_frame->data[0] = std::shared_ptr<uint8_t[]>(new uint8_t[y_size]);
  output_frame->data[1] = std::shared_ptr<uint8_t[]>(new uint8_t[uv_size]);
  output_frame->data[2] = std::shared_ptr<uint8_t[]>(new uint8_t[uv_size]);

  for (int row = 0; row < height; ++row) {
    const uint8_t *src_row = frame->data[0] + static_cast<size_t>(row) * frame->linesize[0];
    uint8_t *dst_y = output_frame->data[0].get() + static_cast<size_t>(row) * width;

    for (int column = 0; column < width; ++column) {
      const uint8_t *pixel = src_row + static_cast<size_t>(column) * 4;
      const int b = pixel[0];
      const int g = pixel[1];
      const int r = pixel[2];
      dst_y[column] = ClampByte(((47 * r + 157 * g + 16 * b + 128) >> 8) + 16);
    }
  }

  for (int row = 0; row < height; row += 2) {
    const uint8_t *row0 = frame->data[0] + static_cast<size_t>(row) * frame->linesize[0];
    const uint8_t *row1 = frame->data[0] + static_cast<size_t>(row + 1) * frame->linesize[0];
    uint8_t *dst_u = output_frame->data[1].get() + static_cast<size_t>(row / 2) * (width / 2);
    uint8_t *dst_v = output_frame->data[2].get() + static_cast<size_t>(row / 2) * (width / 2);

    for (int column = 0; column < width; column += 2) {
      int sum_b = 0;
      int sum_g = 0;
      int sum_r = 0;

      const uint8_t *pixels[4] = {
          row0 + static_cast<size_t>(column) * 4,
          row0 + static_cast<size_t>(column + 1) * 4,
          row1 + static_cast<size_t>(column) * 4,
          row1 + static_cast<size_t>(column + 1) * 4,
      };

      for (const uint8_t *pixel : pixels) {
        sum_b += pixel[0];
        sum_g += pixel[1];
        sum_r += pixel[2];
      }

      const int b = sum_b / 4;
      const int g = sum_g / 4;
      const int r = sum_r / 4;
      dst_u[column / 2] = ClampByte(((-26 * r - 87 * g + 112 * b + 128) >> 8) + 128);
      dst_v[column / 2] = ClampByte(((112 * r - 102 * g - 10 * b + 128) >> 8) + 128);
    }
  }

  return output_frame;
}

std::shared_ptr<I420Frame> ConvertObsSourceFrameToI420(const obs_source_frame *frame) {
  if (frame == nullptr || frame->width == 0 || frame->height == 0 ||
      frame->width % 2 != 0 || frame->height % 2 != 0) {
    return nullptr;
  }

  const int width = static_cast<int>(frame->width);
  const int height = static_cast<int>(frame->height);
  auto output_frame = std::make_shared<I420Frame>();
  output_frame->width = width;
  output_frame->height = height;
  output_frame->timestamp_us = static_cast<int64_t>(frame->timestamp / 1000);

  const size_t y_size = output_frame->GetYPlaneSize();
  const size_t uv_size = output_frame->GetUVPlaneSize();
  output_frame->data[0] = std::shared_ptr<uint8_t[]>(new uint8_t[y_size]);
  output_frame->data[1] = std::shared_ptr<uint8_t[]>(new uint8_t[uv_size]);
  output_frame->data[2] = std::shared_ptr<uint8_t[]>(new uint8_t[uv_size]);

  switch (frame->format) {
    case VIDEO_FORMAT_I420: {
      if (frame->data[0] == nullptr || frame->data[1] == nullptr || frame->data[2] == nullptr) {
        return nullptr;
      }

      const bool y_ok = CopyPlane(frame->data[0], frame->linesize[0], output_frame->data[0].get(),
                                  width, width, height);
      const bool u_ok = CopyPlane(frame->data[1], frame->linesize[1], output_frame->data[1].get(),
                                  width / 2, width / 2, height / 2);
      const bool v_ok = CopyPlane(frame->data[2], frame->linesize[2], output_frame->data[2].get(),
                                  width / 2, width / 2, height / 2);
      return y_ok && u_ok && v_ok ? output_frame : nullptr;
    }

    case VIDEO_FORMAT_NV12: {
      if (frame->data[0] == nullptr || frame->data[1] == nullptr) {
        return nullptr;
      }

      if (!CopyPlane(frame->data[0], frame->linesize[0], output_frame->data[0].get(),
                     width, width, height)) {
        return nullptr;
      }

      for (int row = 0; row < height / 2; ++row) {
        const uint8_t *uv_row = frame->data[1] + static_cast<size_t>(row) * frame->linesize[1];
        uint8_t *u_row = output_frame->data[1].get() + static_cast<size_t>(row) * (width / 2);
        uint8_t *v_row = output_frame->data[2].get() + static_cast<size_t>(row) * (width / 2);
        for (int column = 0; column < width / 2; ++column) {
          u_row[column] = uv_row[column * 2];
          v_row[column] = uv_row[column * 2 + 1];
        }
      }

      return output_frame;
    }

    case VIDEO_FORMAT_UYVY:
    case VIDEO_FORMAT_YUY2: {
      if (frame->data[0] == nullptr || frame->linesize[0] < static_cast<uint32_t>(width * 2)) {
        return nullptr;
      }

      const bool is_uyvy = frame->format == VIDEO_FORMAT_UYVY;
      for (int row = 0; row < height; ++row) {
        const uint8_t *src_row = frame->data[0] + static_cast<size_t>(row) * frame->linesize[0];
        uint8_t *dst_y = output_frame->data[0].get() + static_cast<size_t>(row) * width;
        for (int column = 0; column < width; column += 2) {
          const uint8_t *pair = src_row + static_cast<size_t>(column) * 2;
          dst_y[column] = is_uyvy ? pair[1] : pair[0];
          dst_y[column + 1] = is_uyvy ? pair[3] : pair[2];
        }
      }

      for (int row = 0; row < height; row += 2) {
        const uint8_t *row0 = frame->data[0] + static_cast<size_t>(row) * frame->linesize[0];
        const uint8_t *row1 = frame->data[0] + static_cast<size_t>(row + 1) * frame->linesize[0];
        uint8_t *dst_u = output_frame->data[1].get() + static_cast<size_t>(row / 2) * (width / 2);
        uint8_t *dst_v = output_frame->data[2].get() + static_cast<size_t>(row / 2) * (width / 2);

        for (int column = 0; column < width; column += 2) {
          const uint8_t *pair0 = row0 + static_cast<size_t>(column) * 2;
          const uint8_t *pair1 = row1 + static_cast<size_t>(column) * 2;
          const int u0 = is_uyvy ? pair0[0] : pair0[1];
          const int v0 = is_uyvy ? pair0[2] : pair0[3];
          const int u1 = is_uyvy ? pair1[0] : pair1[1];
          const int v1 = is_uyvy ? pair1[2] : pair1[3];
          dst_u[column / 2] = static_cast<uint8_t>((u0 + u1 + 1) / 2);
          dst_v[column / 2] = static_cast<uint8_t>((v0 + v1 + 1) / 2);
        }
      }

      return output_frame;
    }

    case VIDEO_FORMAT_BGRA: {
      video_data data = {};
      data.data[0] = frame->data[0];
      data.linesize[0] = frame->linesize[0];
      data.timestamp = frame->timestamp;
      return ConvertBgraToI420(&data, width, height);
    }

    default:
      return nullptr;
  }
}

//创建回调：取出 ObsRuntime* 并返回。
void *CreateSourceFrameFilter(obs_data_t *settings, obs_source_t *source) {
  (void)source;
  return reinterpret_cast<ObsRuntime *>(
      static_cast<intptr_t>(obs_data_get_int(settings, "runtime")));
}

void DestroySourceFrameFilter(void *param) {
  (void)param;
}

const char *GetSourceFrameFilterName(void *param) {
  (void)param;
  return "Meeting Source Frame Filter";
}

//fliter处理函数的功能：从摄像头 Source 取得原始视频帧，交给项目自己的采集接口，同时让原帧继续沿 OBS 流程传递。
obs_source_frame *FilterSourceVideoFrame(void *param, obs_source_frame *frame) {
  auto *runtime = static_cast<ObsRuntime *>(param);
  if (runtime != nullptr && frame != nullptr) {
    runtime->HandleSourceVideoFrame(frame);
  }

  return frame;
}

void RegisterSourceFrameFilter() {
  static bool registered = false;
  if (registered) {
    return;
  }

  //配置filter
  obs_source_info filter_info = {};
  filter_info.id = "meeting-source-frame-filter";
  filter_info.type = OBS_SOURCE_TYPE_FILTER;
  filter_info.output_flags = OBS_SOURCE_ASYNC_VIDEO;
  /*
  指定创建、销毁、查询、工作时的处理函数。
  */
  filter_info.create = CreateSourceFrameFilter;
  filter_info.get_name = GetSourceFrameFilterName;
  filter_info.destroy = DestroySourceFrameFilter;
  filter_info.filter_video = FilterSourceVideoFrame;
  obs_register_source(&filter_info);
  registered = true;
}

std::string ExecutablePath() {
#ifdef __APPLE__
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  if (size == 0) {
    return {};
  }

  std::vector<char> buffer(size + 1, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
    return {};
  }

  char resolved[PATH_MAX] = {};
  if (realpath(buffer.data(), resolved) != nullptr) {
    return resolved;
  }
  return buffer.data();
#else
  return {};
#endif
}

std::string BundleResourcesDirectory() {
  const std::string executable_path = ExecutablePath();
  if (executable_path.empty()) {
    return {};
  }

  const fs::path executable_dir = fs::path(executable_path).parent_path();
  const fs::path contents_dir = executable_dir.parent_path();
  if (executable_dir.filename() == "MacOS" &&
      contents_dir.filename() == "Contents") {
    return (contents_dir / "Resources").string();
  }
  return {};
}

std::string ResolveObsSdkDir() {
  const std::string resources_dir = BundleResourcesDirectory();
  if (!resources_dir.empty()) {
    const fs::path bundled_sdk_dir = fs::path(resources_dir) / "obs-sdk";
    if (fs::exists(bundled_sdk_dir / "Frameworks" / "libobs.framework")) {
      return bundled_sdk_dir.string();
    }
  }

  return CAPTURE_OBS_SDK_DIR;
}

//加载插件：
/*
obs_add_module_path:
告诉 OBS 去哪里找插件二进制和插件资源。

%module%:
OBS 会自动替换成插件名，比如 mac-avcapture、mac-capture。

obs_load_all_modules2:
真正加载插件，并返回失败信息。

obs_post_load_modules:
通知所有插件“模块都加载完了”，让它们做后置初始化。
*/
CaptureResult LoadObsModules(const std::string &plugin_path) {
  const std::string module_bin_path =
      plugin_path + "/%module%.plugin/Contents/MacOS";
  const std::string module_data_path =
      plugin_path + "/%module%.plugin/Contents/Resources";

  obs_add_module_path(module_bin_path.c_str(), module_data_path.c_str());

  obs_module_failure_info module_failure_info = {};
  obs_load_all_modules2(&module_failure_info);

  const bool load_success = module_failure_info.count == 0;
  obs_module_failure_info_free(&module_failure_info);

  if (!load_success) {
    return CaptureResult::kInternalError;
  }

  obs_post_load_modules();
  return CaptureResult::kSuccess;
}

CaptureResult EnumObsSourceDeviceList(const char *source_id,
                                      const char *property_name,
                                      CaptureDeviceEnumCallback callback) {
  if (source_id == nullptr || source_id[0] == '\0' ||
      property_name == nullptr || property_name[0] == '\0' || !callback) {
    return CaptureResult::kInvalidArgument;
  }

  obs_properties_t *properties = obs_get_source_properties(source_id);
  if (properties == nullptr) {
    return CaptureResult::kInternalError;
  }

  obs_property_t *property = obs_properties_get(properties, property_name);
  if (property == nullptr) {
    obs_properties_destroy(properties);
    return CaptureResult::kNotFound;
  }

  const size_t item_count = obs_property_list_item_count(property);
  for (size_t index = 0; index < item_count; ++index) {
    const char *device_name = obs_property_list_item_name(property, index);
    const char *device_id = obs_property_list_item_string(property, index);

    if (device_id == nullptr || device_name == nullptr) {
      continue;
    }

    if (!callback(device_id, device_name)) {
      break;
    }
  }

  obs_properties_destroy(properties);
  return CaptureResult::kSuccess;
}

} // namespace

ObsRuntime::~ObsRuntime() {
  UnInit();
}

//ObsRuntime 负责把 CaptureEngine 给出的参数，转化为真正运行起来的 OBS 采集环境。
CaptureResult ObsRuntime::Init(int sample_rate, int channels, int width, int height, int fps) {
  if (m_initialized) {
    return CaptureResult::kAlreadyInitialized;
  }

  if (sample_rate <= 0 || channels <= 0 || width <= 0 || height <= 0 || fps <= 0) {
    return CaptureResult::kInvalidArgument;
  }

  //把声道数channels转化为obs的speaker_layout;
  const speaker_layout speakers = ToSpeakerLayout(channels);
  if (speakers == SPEAKERS_UNKNOWN) {
    return CaptureResult::kInvalidArgument;
  }

  //调用obs_startup，启动 OBS 全局环境
  /*
    "en-US" OBS 使用的 locale，影响插件语言文本。
    nullptr module_config_path，目前我们还没有单独的 OBS 配置目录，所以先不传。
    nullptr profiler_name_store，目前我们还没有接 OBS profiler，所以先不传。
  */
  if (!obs_startup("en-US", nullptr, nullptr)) {
    return CaptureResult::kInternalError;
  }

  //视频配置单，描述视频管线应该怎样工作
  obs_video_info video_info = {};

  /*
  graphics_module	使用哪个图形后端，本项目使用 Metal
  fps_num/fps_den	OBS 按什么帧率工作
  base_width/base_height	内部画布尺寸
  output_width/output_height	最终输出尺寸
  格式、色彩、缩放	帧应当怎样表示和处理
  
  本项目：
  使用 Metal
  按 1280×720 处理
  每秒运行 30 帧
  使用 BGRA
  使用 BT.709 色彩空间
  必要时使用双线性缩放
  */
  const std::string obs_sdk_dir = ResolveObsSdkDir();
  const std::string graphics_module =
      obs_sdk_dir + "/Frameworks/libobs-metal.dylib";
  video_info.graphics_module = graphics_module.c_str();
  video_info.fps_num = static_cast<uint32_t>(fps);
  video_info.fps_den = 1;
  video_info.base_width = static_cast<uint32_t>(width);
  video_info.base_height = static_cast<uint32_t>(height);
  video_info.output_width = static_cast<uint32_t>(width);
  video_info.output_height = static_cast<uint32_t>(height);
  video_info.output_format = VIDEO_FORMAT_BGRA;
  video_info.adapter = 0;
  video_info.gpu_conversion = false;
  video_info.colorspace = VIDEO_CS_709;
  video_info.range = VIDEO_RANGE_PARTIAL;
  video_info.scale_type = OBS_SCALE_BILINEAR;

  //启用配置单激活管线
  if (obs_reset_video(&video_info) != OBS_VIDEO_SUCCESS) {
    obs_shutdown(); //停止
    return CaptureResult::kInternalError;
  }

  //音频配置单，描述音频管线应该怎样工作
  obs_audio_info audio_info = {};
  audio_info.samples_per_sec = static_cast<uint32_t>(sample_rate);
  audio_info.speakers = speakers;

  //启用配置单激活管线
  if (!obs_reset_audio(&audio_info)) {
    obs_shutdown();
    return CaptureResult::kInternalError;
  }

  //加载Module
  const CaptureResult load_modules_result =
      LoadObsModules(obs_sdk_dir + "/PlugIns");
  if (load_modules_result != CaptureResult::kSuccess) {
    obs_shutdown();
    return load_modules_result;
  }

  //注册一种自定义视频 Filter 类型，注意 Init() 中只是注册 Filter 类型，真正的 Filter 实例在创建摄像头 Source 时才附加。
  RegisterSourceFrameFilter();

  //创建 Scene，scene是OBS 用来组织多个 Source 的容器。
  m_default_scene = obs_scene_create("capture_default_scene");
  if (m_default_scene == nullptr) {
    obs_shutdown();
    return CaptureResult::kInternalError;
  }

  //创建 Source 并接入 Scene
  obs_set_output_source(0, obs_scene_get_source(m_default_scene));

  m_sample_rate = sample_rate;
  m_channels = channels;
  m_width = width;
  m_height = height;
  m_fps = fps;

  m_initialized = true;

  return CaptureResult::kSuccess;
}

//原则是：先让数据停止流动，再释放生产和承载数据的资源。
CaptureResult ObsRuntime::UnInit() {
  if (!m_initialized) {
    return CaptureResult::kSuccess;
  }

  obs_set_output_source(0, nullptr);
  UnregisterCallbacks();
  ClearSources();

  if (m_default_scene != nullptr) {
    obs_scene_release(m_default_scene);
    m_default_scene = nullptr;
  }

  obs_shutdown();

  m_sample_rate = 0;
  m_channels = 0;
  m_width = 0;
  m_height = 0;
  m_fps = 0;

  m_initialized = false;

  return CaptureResult::kSuccess;
}

bool ObsRuntime::IsInitialized() const {
  return m_initialized;
}

//添加默认视频源
CaptureResult ObsRuntime::AddCameraSource(const std::string &name) {
  return AddObsSource("macos-avcapture", name, nullptr, true);
}

//添加默认音频源
CaptureResult ObsRuntime::AddMicSource(const std::string &name) {
  return AddObsSource("coreaudio_input_capture", name, nullptr, false);
}

//添加指定设备的视频源
CaptureResult ObsRuntime::AddCameraSource(const std::string &name, const std::string &device_id) {
  if (device_id.empty()) {
    return CaptureResult::kInvalidArgument;
  }

  //创建一张空配置表
  obs_data *settings = obs_data_create();
  if (settings == nullptr) {
    return CaptureResult::kInternalError;
  }

  //往配置表里填入配置
  /*
  "device": "具体摄像头ID",
  "use_preset": true,
  "preset": "AVCaptureSessionPreset1280x720",
  "enable_audio": false
  */
  obs_data_set_string(settings, "device", device_id.c_str());
  obs_data_set_bool(settings, "use_preset", true);
  obs_data_set_string(settings, "preset", CameraPresetForSize(m_width, m_height));
  obs_data_set_bool(settings, "enable_audio", false);

  //添加obs源，以下操作同默认源创建
  const CaptureResult result =
      AddObsSource("macos-avcapture", name, settings, true);
  //释放的是当前函数持有的临时配置对象引用，不是刚创建的摄像头 Source。
  obs_data_release(settings);

  return result;
}

CaptureResult ObsRuntime::AddMicSource(const std::string &name, const std::string &device_id) {
  if (device_id.empty()) {
    return CaptureResult::kInvalidArgument;
  }

  obs_data *settings = obs_data_create();
  if (settings == nullptr) {
    return CaptureResult::kInternalError;
  }

  obs_data_set_string(settings, "device_id", device_id.c_str());
  const CaptureResult result =
      AddObsSource("coreaudio_input_capture", name, settings, false);
  obs_data_release(settings);

  return result;
}

//切换设备
CaptureResult ObsRuntime::UpdateCameraSourceDevice(const std::string &name,
                                                   const std::string &device_id) {
  if (!m_initialized) {
    return CaptureResult::kNotInitialized;
  }
  if (name.empty() || device_id.empty()) {
    return CaptureResult::kInvalidArgument;
  }

  const auto source_it = m_sources.find(name);
  if (source_it == m_sources.end() || source_it->second == nullptr) {
    return CaptureResult::kNotFound;
  }

  obs_data *settings = obs_source_get_settings(source_it->second);
  if (settings == nullptr) {
    return CaptureResult::kInternalError;
  }

  /*
  device	选择哪台摄像头
  use_preset	是否采用摄像头插件支持的采集预设
  preset	具体采用哪一档预设
  enable_audio	是否让摄像头 Source 同时采集音频，本项目关闭
  */
  obs_data_set_string(settings, "device", device_id.c_str());
  obs_data_set_bool(settings, "use_preset", true);
  obs_data_set_string(settings, "preset", CameraPresetForSize(m_width, m_height));
  obs_data_set_bool(settings, "enable_audio", false); //不启用音频，防止收音混乱。

  /*
  source_it->second → 要更新的具体 Source 实例
  settings          → 希望它应用的配置
  */
  obs_source_update(source_it->second, settings);

  //这里释放的是内存中配置对象的当前引用
  obs_data_release(settings);
  return CaptureResult::kSuccess;
}

CaptureResult ObsRuntime::UpdateMicSourceDevice(const std::string &name,
                                                const std::string &device_id) {
  if (!m_initialized) {
    return CaptureResult::kNotInitialized;
  }
  if (name.empty() || device_id.empty()) {
    return CaptureResult::kInvalidArgument;
  }

  const auto source_it = m_sources.find(name);
  if (source_it == m_sources.end() || source_it->second == nullptr) {
    return CaptureResult::kNotFound;
  }

  obs_data *settings = obs_source_get_settings(source_it->second);
  if (settings == nullptr) {
    return CaptureResult::kInternalError;
  }

  obs_data_set_string(settings, "device_id", device_id.c_str());
  obs_source_update(source_it->second, settings);
  obs_data_release(settings);
  return CaptureResult::kSuccess;
}

CaptureResult ObsRuntime::EnumCameraDevices(CaptureDeviceEnumCallback callback) {
  if (!m_initialized) {
    return CaptureResult::kNotInitialized;
  }

  if (!callback) {
    return CaptureResult::kInvalidArgument;
  }

  std::vector<std::pair<std::string, std::string>> devices;
#ifdef __APPLE__
  devices = MAC::GetCameraDevices();
#endif

  auto collect = [&devices](const char *device_id, const char *device_name) {
    devices.emplace_back(device_id ? device_id : "",
                         device_name ? device_name : "");
    return true;
  };

  CaptureResult result = CaptureResult::kSuccess;
  if (devices.empty()) {
    result = EnumObsSourceDeviceList("macos-avcapture-fast", "device", collect);
  }
  if (devices.empty() &&
      (result != CaptureResult::kSuccess || devices.empty())) {
    devices.clear();
    result = EnumObsSourceDeviceList("macos-avcapture", "device", collect);
  }

  if (result != CaptureResult::kSuccess) {
    return result;
  }

  for (const auto &device : devices) {
    if (!callback(device.first.c_str(), device.second.c_str())) {
      break;
    }
  }

  return CaptureResult::kSuccess;
}

CaptureResult ObsRuntime::EnumMicDevices(CaptureDeviceEnumCallback callback) {
  if (!m_initialized) {
    return CaptureResult::kNotInitialized;
  }

  if (!callback) {
    return CaptureResult::kInvalidArgument;
  }

#ifdef __APPLE__
  const auto devices = MAC::GetMicrophoneDevices();
  if (!devices.empty()) {
    for (const auto &device : devices) {
      if (!callback(device.first.c_str(), device.second.c_str())) {
        break;
      }
    }
    return CaptureResult::kSuccess;
  }
#endif

  return EnumObsSourceDeviceList("coreaudio_input_capture", "device_id", std::move(callback));
}

CaptureResult ObsRuntime::RemoveSource(const std::string &name) {
  if (!m_initialized) {
    return CaptureResult::kNotInitialized;
  }

  if (name.empty()) {
    return CaptureResult::kInvalidArgument;
  }

  auto source_it = m_sources.find(name);
  auto scene_item_it = m_scene_items.find(name);
  auto filter_it = m_source_frame_filters.find(name);

  if (source_it == m_sources.end() && scene_item_it == m_scene_items.end() &&
      filter_it == m_source_frame_filters.end()) {
    return CaptureResult::kNotFound;
  }

  RemoveSourceFrameFilter(
      name, source_it != m_sources.end() ? source_it->second : nullptr);

  if (scene_item_it != m_scene_items.end()) {
    obs_sceneitem_remove(scene_item_it->second);
    m_scene_items.erase(scene_item_it);
  }
  if (source_it != m_sources.end()) {
    obs_source_release(source_it->second);
    m_sources.erase(source_it);
  }

  return CaptureResult::kSuccess;
}

//给souce装配fliter
/*
把 ObsRuntime 地址写入配置
→ 创建私有 Filter 实例
→ 把 Filter 挂到 Camera Source
→ 保存 Filter，供后续拆卸
*/
CaptureResult ObsRuntime::AddSourceFrameFilter(const std::string &name, obs_source *source) {
  if (name.empty() || source == nullptr) {
    return CaptureResult::kInvalidArgument;
  }

  if (m_source_frame_filters.find(name) != m_source_frame_filters.end()) {
    return CaptureResult::kAlreadyInitialized;
  }

  obs_data *settings = obs_data_create();
  if (settings == nullptr) {
    return CaptureResult::kInternalError;
  }

  /*
  它本质上是一个 C API 到 C++ 对象的回调桥梁。
  obs_data_set_int 与 obs_data_get_int 配对使用。
  obs_data_set_int：
  OBS 的 C 回调怎样找到当前这个 C++ ObsRuntime 对象？ 
  做两层转化：
  reinterpret_cast<intptr_t>(this)：把指针转换为可以容纳地址的整数。
  static_cast<long long>(...)：适配 obs_data_set_int() 要求的参数类型。

  ！注意：Filter 保存的 ObsRuntime* 只是借用地址，不会延长 ObsRuntime 的寿命，因此销毁 Runtime 时必须妥善停止相关回调、拆除 Filter。
  */
  //准备配置，具体来说是将当前的ObsRuntime的地址放入这个API。
  obs_data_set_int(settings, "runtime", static_cast<long long>(reinterpret_cast<intptr_t>(this)));
  const std::string filter_name = name + "_source_frame_filter";

  /*
  创建私有Fliter实例
  "meeting-source-frame-filter" → Filter 类型 ID
  filter_name                   → Filter 实例名称
  settings                      → 创建配置，其中包含 ObsRuntime*
  */
  obs_source *filter =
      obs_source_create_private("meeting-source-frame-filter", filter_name.c_str(), settings);
  obs_data_release(settings);

  if (filter == nullptr) {
    return CaptureResult::kInternalError;
  }

  //挂载Filter实例到指定的Source上
  obs_source_filter_add(source, filter);

  //失败回滚
  try {
    //保存管理记录，为销毁做准备
    const auto insert_result = m_source_frame_filters.emplace(name, filter);
    if (!insert_result.second) {
      obs_source_filter_remove(source, filter);
      obs_source_release(filter);
      return CaptureResult::kAlreadyInitialized;
    }
  } catch (...) {
    obs_source_filter_remove(source, filter);
    obs_source_release(filter);
    return CaptureResult::kInternalError;
  }
  return CaptureResult::kSuccess;
}

void ObsRuntime::RemoveSourceFrameFilter(const std::string &name,
                                         obs_source *source) {
  const auto filter_it = m_source_frame_filters.find(name);
  if (filter_it == m_source_frame_filters.end()) {
    return;
  }

  if (source != nullptr) {
    obs_source_filter_remove(source, filter_it->second);
  }
  obs_source_release(filter_it->second);
  m_source_frame_filters.erase(filter_it);
}

void ObsRuntime::ClearSourceFrameFilters() {
  for (auto &item : m_source_frame_filters) {
    auto source_it = m_sources.find(item.first);
    if (source_it != m_sources.end()) {
      obs_source_filter_remove(source_it->second, item.second);
    }
    obs_source_release(item.second);
  }

  m_source_frame_filters.clear();
}

/*
  真实 obs_source
  Scene 中的 obs_scene_item
  附加在 Source 上的 FrameFilter
*/
//AddObsSource() 的本质是一次多资源事务式创建
/*
检查前置状态
    ↓
创建真实 OBS Source
    ↓
按需安装视频帧 Filter
    ↓
把准备完成的 Source 加入 Scene，建立 SceneItem 关系
    ↓
记录 Source
    ↓
记录 SceneItem
    ↓
全部成功才返回 Success
*/
CaptureResult ObsRuntime::AddObsSource(const std::string &source_id,
                                       const std::string &name,
                                       obs_data *settings,
                                       bool attach_frame_filter) {
  if (!m_initialized) {
    return CaptureResult::kNotInitialized;
  }

  if (source_id.empty() || name.empty()) {
    return CaptureResult::kInvalidArgument;
  }

  if (m_sources.find(name) != m_sources.end()) {
    return CaptureResult::kAlreadyInitialized;
  }

  if (m_default_scene == nullptr) {
    return CaptureResult::kInternalError;
  }

  /*
  id: source类型
  name: source名称
  settings:使用哪个配置，（会增加被使用的settings对象的引用计数）
  hotkey_data：热键设置，本项目不使用
  创建完毕后得到一个引用，后面需要释放。
  */
  obs_source *source = obs_source_create(source_id.c_str(), name.c_str(), settings, nullptr);
  if (source == nullptr) {
    return CaptureResult::kInternalError;
  }

  //装配Filter
  if (attach_frame_filter) {
    CaptureResult filter_result = CaptureResult::kInternalError;
    try {
      filter_result = AddSourceFrameFilter(name, source);
    } catch (...) {
      obs_source_release(source);
      return CaptureResult::kInternalError;
    }
    if (filter_result != CaptureResult::kSuccess) {
      obs_source_release(source);
      return filter_result;
    }
  }

  //接入 Scene
  obs_scene_item *scene_item = obs_scene_add(m_default_scene, source);
  if (scene_item == nullptr) {
    RemoveSourceFrameFilter(name, source);
    obs_source_release(source);
    return CaptureResult::kInternalError;
  }

  bool source_inserted = false;
  try {
    //提交管理关系，让 ObsRuntime 后续能够根据名字找到资源
    const auto source_result = m_sources.emplace(name, source);
    //失败回滚
    if (!source_result.second) {
      RemoveSourceFrameFilter(name, source);
      obs_sceneitem_remove(scene_item);
      obs_source_release(source);
      return CaptureResult::kAlreadyInitialized;
    }
    source_inserted = true;

    //提交管理关系，让 ObsRuntime 后续能够根据名字找到资源
    const auto scene_item_result = m_scene_items.emplace(name, scene_item);
    //失败回滚
    if (!scene_item_result.second) {
      m_sources.erase(name);
      RemoveSourceFrameFilter(name, source);
      obs_sceneitem_remove(scene_item);
      obs_source_release(source);
      return CaptureResult::kInternalError;
    }
  } catch (...) {
    if (source_inserted) {
      m_sources.erase(name);
    }
    RemoveSourceFrameFilter(name, source);
    obs_sceneitem_remove(scene_item);
    obs_source_release(source);
    return CaptureResult::kInternalError;
  }

  return CaptureResult::kSuccess;
}

void ObsRuntime::ClearSources() {
  ClearSourceFrameFilters();

  for (auto &item : m_scene_items) {
    obs_sceneitem_remove(item.second);
  }

  for (auto &item : m_sources) {
    obs_source_release(item.second);
  }

  m_scene_items.clear();
  m_sources.clear();
}

CaptureResult ObsRuntime::RegisterVideoCallback(VideoDataCallback callback) {
  if (!m_initialized) {
    return CaptureResult::kNotInitialized;
  }

  if (!callback) {
    return CaptureResult::kInvalidArgument;
  }

  std::atomic_store(&m_video_callback,
                    std::make_shared<const VideoDataCallback>(std::move(callback)));

  return CaptureResult::kSuccess;
}

//注册音频回调
CaptureResult ObsRuntime::RegisterAudioCallback(AudioDataCallback callback) {
  if (!m_initialized) {
    return CaptureResult::kNotInitialized;
  }

  if (!callback) {
    return CaptureResult::kInvalidArgument;
  }

  std::atomic_store(&m_audio_callback,
                    std::make_shared<const AudioDataCallback>(std::move(callback)));

  if (!m_audio_callback_registered) {
    /*
    注册obs回调
    从哪一路混音输出取音频？Mix 0
    是否额外指定交付给回调的音频格式？不指定
    有音频时调用哪个函数？&ObsRuntime::OnRawAudioFrame	
    调用函数时，把哪个对象的地址一起传过去？this
    */
    obs_add_raw_audio_callback(0, nullptr, &ObsRuntime::OnRawAudioFrame, this);
    m_audio_callback_registered = true;
  }

  return CaptureResult::kSuccess;
}

void ObsRuntime::UnregisterCallbacks() {
  std::atomic_store(&m_video_callback, std::shared_ptr<const VideoDataCallback>{});
  std::atomic_store(&m_audio_callback, std::shared_ptr<const AudioDataCallback>{});

  if (m_audio_callback_registered) {
    obs_remove_raw_audio_callback(0, &ObsRuntime::OnRawAudioFrame, this);
    m_audio_callback_registered = false;
  }
}

void ObsRuntime::OnRawVideoFrame(void *param, video_data *frame) {
  auto *runtime = static_cast<ObsRuntime *>(param);
  if (runtime == nullptr) {
    return;
  }

  runtime->HandleRawVideoFrame(frame);
}

//当有音频到达时的处理函数
void ObsRuntime::OnRawAudioFrame(void *param, size_t mix_idx, audio_data *data) {
  auto *runtime = static_cast<ObsRuntime *>(param);
  if (runtime == nullptr) {
    return;
  }

  runtime->HandleRawAudioFrame(mix_idx, data);
}

std::shared_ptr<I420Frame> ObsRuntime::ConvertVideoFrame(const video_data *frame) const {
  if (frame == nullptr || m_width <= 0 || m_height <= 0) {
    return nullptr;
  }

  if (m_width % 2 != 0 || m_height % 2 != 0) {
    return nullptr;
  }

  return ConvertBgraToI420(frame, m_width, m_height);
}

void ObsRuntime::HandleRawVideoFrame(video_data *frame) {
  const auto registered_callback = std::atomic_load(&m_video_callback);
  if (frame == nullptr || !registered_callback) {
    return;
  }

  auto converted_frame = ConvertVideoFrame(frame);
  if (converted_frame == nullptr || converted_frame->Validate() != CaptureResult::kSuccess) {
    return;
  }

  auto callback = *registered_callback;
  callback(converted_frame);
}

//将视频帧frame 转化为 I420Frame 然后进行上层视频回调。
void ObsRuntime::HandleSourceVideoFrame(const obs_source_frame *frame) {
  const auto registered_callback = std::atomic_load(&m_video_callback);
  if (frame == nullptr || !registered_callback) {
    return;
  }

  auto converted_frame = ConvertObsSourceFrameToI420(frame);
  if (converted_frame == nullptr || converted_frame->Validate() != CaptureResult::kSuccess) {
    return;
  }

  auto callback = *registered_callback;
  callback(converted_frame);
}

//处理函数
void ObsRuntime::HandleRawAudioFrame(size_t mix_idx, audio_data *data) {
  const auto registered_callback = std::atomic_load(&m_audio_callback);
  if (data == nullptr || !registered_callback) {
    return;
  }

  //当前音频只注册 mix 0，后续支持多音轨时需要处理 mix_idx。
  (void)mix_idx;

  /*
  转化：
  统一表示：把 OBS 的音频数据整理成项目约定的格式，并填写采样率、声道数、样本数和时间戳。
  独立持有：给 AudioFrame 分配自己的数据空间，上层不必依赖 OBS 本次回调提供的临时数据。
  */
  auto converted_frame = ConvertAudioFrame(data);
  if (converted_frame == nullptr || converted_frame->Validate() != CaptureResult::kSuccess) {
    return;
  }

  auto callback = *registered_callback;
  callback(converted_frame);
}

std::shared_ptr<AudioFrame> ObsRuntime::ConvertAudioFrame(const audio_data *data) const {
  if (data == nullptr || data->frames == 0 || m_sample_rate <= 0 || m_channels <= 0) {
    return nullptr;
  }

  for (int channel = 0; channel < m_channels; ++channel) {
    if (data->data[channel] == nullptr) {
      return nullptr;
    }
  }

  auto output_frame = std::make_shared<AudioFrame>();
  output_frame->samples = static_cast<int>(data->frames);
  output_frame->channels = m_channels;
  output_frame->sample_rate = m_sample_rate;
  output_frame->timestamp_us = static_cast<int64_t>(data->timestamp / 1000);

  const size_t sample_count = static_cast<size_t>(output_frame->samples);
  const size_t channel_count = static_cast<size_t>(output_frame->channels);
  const size_t total_float_count = sample_count * channel_count;
  const size_t total_bytes = total_float_count * sizeof(float);

  output_frame->data = std::shared_ptr<uint8_t[]>(new uint8_t[total_bytes]);

  auto *dst = reinterpret_cast<float *>(output_frame->data.get());

  // 当前按 OBS float planar 音频处理，后续需要根据 audio conversion 明确格式约束。
  for (size_t sample = 0; sample < sample_count; ++sample) {
    for (size_t channel = 0; channel < channel_count; ++channel) {
      const auto *src = reinterpret_cast<const float *>(data->data[channel]);
      dst[sample * channel_count + channel] = src[sample];
    }
  }

  return output_frame;
}

} // namespace CAPTURE
