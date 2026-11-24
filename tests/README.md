# Tests

这里用于存放单元测试和集成测试。

优先测试 Engine 层的状态、数据处理和边界条件；UI 测试只覆盖关键交互流程。

## 自动测试

默认 `ctest` 只放入本地可重复、对外部环境依赖较少的测试：

- `LoggerSmokeTest`：验证普通日志宏和流式日志宏。
- `HttpClientSmokeTest`：启动本机临时 HTTP 服务，验证 GET/POST/DELETE。
- `VceEngineServiceCallbackTest`：验证会控事件回调与会议列表。
- `VceEngineServiceTransportIntegrationTest`：验证会控结果能正确设置传输房间信息。
- `VceEngineTransportManagerTest`、`TransportManagerQueueTest`：验证传输管理和队列。
- `TransportCoreLocalEngineTest`、`TransportCoreSrsEngineTest`：验证传输核心、本地回环和 SRS 端点构建。
- `RenderAudioMixerTest`、`RenderValidationTest`：验证渲染侧音频混音和基础参数校验。

## 手动联调

这些测试不默认进入 `ctest`，因为它们依赖真实设备或真实服务：

- `GrpcServiceManagerSmokeTest`：需要真实 `Meeting-Server` 和 SRS。
- `AVFoundationCameraRenderSmokeTest`、`CaptureRenderAVSmokeTest`：需要摄像头/麦克风权限。
- `CaptureCallbackSmokeTest`、`CaptureObsRuntimeTest`、`CaptureEngineObsIntegrationTest`：用于 OBS 采集链路手动验证。`CaptureObsRuntimeTest` 还使用合成 I420 帧检查回调内替换和跨线程替换，新增检查在 Release 下也生效。测试仍需成功初始化本机 OBS 和 Metal。

## 二开基线

后续做音视频二开时，建议先保证自动测试全部通过，再进行真实联调。真实联调结果应记录：

- 服务端地址、SRS 地址和测试客户端数量。
- 推流、拉流、加入、退出和结束会议是否成功。
- 失败时对应日志、错误码和复现步骤。

## 引擎生命周期回归

`EngineLifecycleRegressionTest` 使用 Mock 模块验证直接观察者回调重入、初始化失败回滚、恢复及三轮初始化/退出；无需真实设备或服务，CTest 超时为 15 秒。
详细改动与验证边界见 [工程改进记录](../docs/engineering-improvements.md)。

`GrpcServiceDeadlineTest` 使用本机随机端口的慢 gRPC 服务，验证普通 RPC 超时、失败输出保持和恢复请求；无需远端服务或真实设备，CTest 超时为 15 秒。
