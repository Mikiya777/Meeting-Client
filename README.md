# Meeting-Client

基于 Qt 6、OBS、WebRTC/SRS、gRPC 的视频会议客户端。

当前项目重点不是完整 UI，而是把客户端核心媒体链路搭起来：

- 采集：摄像头、麦克风、OBS source 和音视频帧回调
- 渲染：视频用户管理、YUV/OpenGL 渲染入口、音频混音与播放
- 传输：本地回环、SRS、WHIP/WHEP、推流、拉流、编解码
- 会控：用户、会议、成员事件、gRPC 事件订阅
- 网络：统一 HTTP GET/POST/DELETE 和公网 IP 获取
- 日志：统一日志入口、等级、控制台和文件输出

## 目录结构

```text
Meeting-Client/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── app/                       # 日志、皮肤和 UI 事件调度
│   ├── capture/                   # 音视频采集及平台后端
│   ├── render/                    # 音视频渲染封装
│   ├── platform/                  # 采集与播放共用的平台设备代码
│   ├── transport/                 # WebRTC/SRS 传输封装
│   ├── engine/                    # 串联采集、渲染、传输、会控
│   ├── network/                   # libcurl HTTP 封装
│   ├── log/                       # spdlog 日志封装
│   └── ui/                        # 简化会议窗口
│       ├── MeetingWindow.*        # 页面和窗口
│       └── widgets/
│           ├── MeetingControlBar.* # 会议控制栏
│           ├── MeetingSidePanel.*  # 成员和聊天侧栏
│           ├── VideoGridWidget.*   # 动态视频宫格
│           └── VideoTileWidget.*   # OpenGL 视频显示入口
├── packaging/windows/            # Windows 打包脚本与依赖清单
├── resources/                     # QSS 等 Qt 资源
├── translations/                  # Qt 国际化翻译
└── tests/                         # 单元测试与集成测试
```

### 平台代码

| 功能 | Windows | macOS |
| --- | --- | --- |
| 采集 | [Qt 摄像头](src/capture/src/windows/QtCaptureRuntime.cpp) | [OBS 采集](src/capture/src/macos/ObsRuntime.cpp)、[原生设备](src/capture/src/macos/MacCaptureDevice.mm) |
| 音频设备与播放 | [WASAPI 设备](src/platform/windows/audio/WasapiDevice.cpp)、[播放接入](src/render/src/audio/windows/WasapiPlayback.cpp) | [AudioQueue 播放](src/render/src/audio/macos/AudioQueuePlayback.cpp) |

Win7 和 Win10 共用 Windows 目录，摄像头代码按 Qt 版本选择实现。混音、回声处理、编码和会议逻辑由各平台共用。

## 依赖方向

```text
main -> ui -> engine -> capture/render/transport/service
                  transport -> network
                  service -> network
                  app/ui/engine/network/transport -> log
```

- `engine` 不依赖 `ui`。
- `ui` 负责展示和用户交互，不承载音视频或业务核心逻辑。
- 公共小控件放入 `ui/widgets`，完整页面或窗口直接放入 `ui`。
- `network` 和 `log` 是底层通用模块，业务代码不直接操作 libcurl 或 spdlog。

Engine 的后台线程事件需要通过 `UiEventDispatcher::Post()` 切换到 Qt
主线程后再更新界面。

## 当前边界

本项目已经具备课程文档要求的客户端核心模块。为了给后续二开留空间，暂时没有把以下能力产品化：

- 完整登录、创建会议、加入会议等 UI 流程。
- 从 Engine 公共接口暴露设备枚举、设备切换、扬声器选择。
- 真实服务端 + 真实 SRS + 多客户端的长时间联调报告。
- 弱网、音视频同步、质量统计、自适应编码等进阶能力。

## 构建

```bash
cmake -S . -B build
cmake --build build
```

## 测试

可重复本地测试：

```bash
ctest --test-dir build-grpc --output-on-failure
```

当前自动测试覆盖日志、网络、会控事件、Engine 串联、传输队列、本地传输、SRS 端点构建、渲染音频混音和渲染基础校验。

手动联调测试：

- `GrpcServiceManagerSmokeTest`：需要真实服务端和 SRS。
- 采集设备测试：需要本机摄像头/麦克风权限。
- 多客户端推拉流：需要真实 SRS WHIP/WHEP 服务。

## Windows 测试包

- Windows 10 x64 使用 Qt 6、VS 2022 和 WASAPI。构建、打包和联调见 [Windows 测试包](docs/windows-test-build.md)。
- Windows 7 x64 使用 Qt 5.15.2、VS 2019 v142 和 `MEETING_WINDOWS_7=ON`。见 [Windows 7 构建](docs/windows-test-build.md#windows-7-x64-构建)。

Windows 和 macOS 共用会议、WebRTC 和麦克风回声处理代码。详见 [回声消除实测记录](docs/acoustic-echo-cancellation.md)。
