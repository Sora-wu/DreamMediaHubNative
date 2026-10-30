# DreamMediaHubNative

DreamMediaHub 的独立音视频 native 库，包含基于 FFmpeg 的 DreamPlayer、DreamRecorder 和公共头文件，使用 C++23。对外继续提供 CMake 包 `Dream` 和目标 `Dream::Player`、`Dream::Recorder`。

## 获取源码

```bash
git clone --recurse-submodules git@github.com:Sora-wu/DreamMediaHubNative.git
# 已有仓库
git submodule update --init --recursive
```

FFmpeg 位于 `third/src/ffmpeg`，保留拆分前的版本 `fe01189b0afd31d4888caf1043d52d49827ce7e1`（4.3.9）。FFmpeg 开发包和构建产物不提交到仓库。

## Windows

需要 CMake 3.21+、支持 C++23 的 MSVC x64（v143 工具集）和 Windows SDK。将同一套 FFmpeg 4.x x64 共享库开发包放到：

```text
third/build/win/
  include/libavcodec/avcodec.h
  include/libavformat/avformat.h
  ...
  lib/avcodec.lib
  lib/avformat.lib
  ...
  bin/avcodec-58.dll
  bin/avformat-58.dll
  ...（包含 FFmpeg 依赖的其他 DLL）
```

在仓库根目录执行（Visual Studio 2022 可改用 `Visual Studio 17 2022`）：

```powershell
cmake -S . -B build/windows-msvc -G "Visual Studio 18 2026" -A x64 -T v143 -DCMAKE_INSTALL_PREFIX="E:/path/to/install"
cmake --build build/windows-msvc --config Debug --parallel
cmake --install build/windows-msvc --config Debug
```

## Linux

需要支持 C++23 的编译器、CMake，以及 FFmpeg 所需的 x264、x265、fdk-aac 开发依赖。可运行 `./build_dep.sh` 将 FFmpeg 编译到 `third/build/linux`，或将兼容的 FFmpeg 开发包放到该目录。

```bash
./build_dep.sh
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=/path/to/install
cmake --build build/linux --parallel
cmake --install build/linux
```

## 在其他项目中使用

将安装目录加入 `CMAKE_PREFIX_PATH` 后：

```cmake
find_package(Dream COMPONENTS Player Recorder REQUIRED)
target_link_libraries(your_player PRIVATE Dream::Player)
target_link_libraries(your_recorder PRIVATE Dream::Recorder)
```

DreamMediaHub 将本仓库作为 `third/DreamMediaHubNative` 子模块引入。其根目录的 `build_dep.bat` / `build_dep.sh` 负责构建本仓库并安装到 DreamMediaHub 的 `third/build/<平台>`，前端通过上述 CMake 包使用 native 库。
