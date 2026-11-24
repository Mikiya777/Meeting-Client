# Meeting-Client

Meeting-Client 是一个支持 Windows 和 macOS 的桌面视频会议客户端，使用 C++、Qt、gRPC 和 WebRTC。它支持创建和加入会议、切换摄像头和麦克风，并提供实时回声消除。

## 使用

通话需要兼容 [会控协议](protos/) 的 gRPC 服务和可访问的 SRS 媒体服务。

1. 打开客户端，点击 **新会议**，填写用户名和会议服务器地址。
2. 将会议 ID 发给另一端。另一端点击 **加入会议**，填写同一个服务器地址、不同的用户名和会议 ID。
3. 进入会议后，通过底部工具栏开启摄像头和麦克风。首次使用时，允许系统访问这两个设备。

## 构建和启动

### Windows

已有 Windows 测试包时，完整解压后运行 `MeetingClient.exe`。

从源码构建 Windows 10 x64 版，需要 Visual Studio 2022、CMake、Qt 6.11.1（含 Qt Multimedia）和 vcpkg。在项目根目录打开 PowerShell，将示例路径换成自己的安装路径。

```powershell
.\packaging\windows\build.ps1 -QtDir 'C:\Qt\6.11.1\msvc2022_64' -VcpkgRoot 'C:\vcpkg'
```

构建完成后，测试包位于 `out/MeetingClient-Windows-x64.zip`。Windows 7 使用 Qt 5.15.2 和 VS 2019 v142，配置 CMake 时启用 `-DMEETING_WINDOWS_7=ON`。

### macOS

Apple Silicon 构建需要 Xcode Command Line Tools、CMake、Qt 6、vcpkg 和 [本地 OBS SDK](src/capture/third_party/obs/README.md)。用 vcpkg 安装 gRPC、Protobuf、libdatachannel（含 SRTP）、OpenCV、SpeexDSP、spdlog 和 nlohmann-json，并让 `pkg-config` 能找到 libcurl、Opus、OpenH264 和 libsamplerate。

将 `QT_DIR`、`VCPKG_ROOT` 和 `OBS_SDK_DIR` 设为实际安装路径，在项目根目录运行。

```bash
cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=arm64-osx -DCAPTURE_OBS_SDK_DIR="$OBS_SDK_DIR"
cmake --build build-macos --parallel
./build-macos/app
```
