# OpenCV 轻度美颜（第一版）

## 使用

启动现有会议客户端，在底部摄像头控制旁点击“美颜关闭 / 美颜开启”。默认关闭，固定强度，无调节滑块。关闭摄像头再打开保留本次开关状态；引擎 Uninit 后恢复关闭。

## 接入路径

`MeetingControlBar::BeautyToggled → MeetingWindow → VceEngine::SetBeautyEnabled → VceEngineImpl → BeautyProcessor`

`OnCaptureVideoFrame → BeautyProcessor::Process → 本地预览 + PushVideoFrame → 现有传输/编码`

本地预览和发送使用同一份处理结果，不处理远端接收画面。

## 算法

直接在 I420 的 Y（亮度）平面使用 OpenCV bilateralFilter：邻域直径 5、sigmaColor=18、sigmaSpace=3；输出 = 65% 原图 + 35% 平滑结果。亮度中的细小变化适度减弱，强边缘相对保留。U/V 色度保持原样，避免 BGR 往返转换的开销和色彩范围问题。

这是全画面轻度柔化，不识别人脸或皮肤，背景和衣物也会受到影响；无美白、瘦脸、大眼。效果刻意轻微，可在固定光照、相同姿势下开关比较脸颊细节。滤波器不理解语义，不能保证五官细节完全不受影响。

## 帧与线程

沿用现有紧密排列的 I420 帧约定；保留尺寸、user_id、timestamp_us。开启时只为输出 Y 新建独立数据，U/V 共享只读，绝不原地修改采集帧。关闭时直接返回输入，不进行滤波或新增像素拷贝。空帧、非法帧或处理异常旁路；异常输出诊断信息。

开关使用 atomic，图像中间数据为每次调用的局部变量。切换从后续采样到新状态的帧生效，已经处理或排队的帧可能稍后显示。本版同步运行在现有采集回调线程，未增加队列或 GPU 路径；高分辨率表现需要在实际设备测量。

## 依赖与构建

需要 OpenCV 4 的 core、imgproc。沿用项目 vcpkg 时：

```sh
/Users/mac/vcpkg/vcpkg install 'opencv4[core]:arm64-osx'
cmake -S . -B build-grpc
cmake --build build-grpc -j4
ctest --test-dir build-grpc --output-on-failure
```

也支持提供 OpenCV_DIR（含 OpenCVConfig.cmake 的目录）。不依赖 OpenCV 窗口系统或人脸模型。

## 手动验收

1. 正常进入会议，打开摄像头，确认默认“美颜关闭”。
2. 同一光照下开启，比较脸颊细节，再关闭，确认恢复原图。
3. 连续切换，关闭/重开摄像头，检查画面持续更新。
4. 两客户端入同一会议，从对端确认开关作用于发送画面。

自动测试覆盖关闭旁路、噪声减弱与强边缘保留、输入数据不变、色度/时间戳保留、尺寸变化、Engine 预览/发送一致及 Qt 按钮开关。实际摄像头主观观感和双端网络效果仍需手动验收。
