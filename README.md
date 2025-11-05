# use_ffmpeg

一个极简的基于 Qt6 的视频播放器示例，演示如何在 Windows 上使用 vcpkg 提供的静态 FFmpeg 库进行集成。

主要内容
- 演示使用 Qt6 (Core, Widgets, Multimedia) 播放视频并做简单的网络流处理。
- 项目代码位于仓库根目录（`main.cpp`、`VideoPlayer.cpp`、`AudioProcessor.cpp` 等）。

快速开始（Windows + cmd）
1. 安装依赖：
   - 安装 Qt6（示例中使用的路径：`D:/Qt/6.9.1/msvc2022_64`）。
   - 安装并配置 vcpkg（确保环境变量 `VCPKG_ROOT` 已设置）。
   - 通过 vcpkg 安装所需包（示例）：

```cmd
vcpkg install ffmpeg[aom,mp3lame,openjpeg,opus,vpx,x264]:x64-windows-static
vcpkg install libx264:x64-windows-static libx265:x64-windows-static aom:x64-windows-static libvpx:x64-windows-static libmp3lame:x64-windows-static openjpeg:x64-windows-static opus:x64-windows-static
```

2. 生成工程并构建：

```cmd
cmake -B build -S . -DCMAKE_PREFIX_PATH="D:/Qt/6.9.1/msvc2022_64" -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release
```

注意事项（静态 FFmpeg）
- 本项目示例默认使用 vcpkg 的 x64-windows-static 库。静态库通常使用静态运行时（/MT），这可能与 Qt 或其他库使用的动态运行时（/MD）冲突。你会在链接阶段看到类似于 RuntimeLibrary 不匹配的错误。
- 解决方法：
  - 使用与 vcpkg 安装时相匹配的运行时（如果你选择静态 triplet），确保所有第三方静态库都用相同运行时构建；或
  - 使用 vcpkg 的动态 triplet（x64-windows）来避免 /MT 与 /MD 的冲突；或
  - 重新编译第三方库以统一运行时（高级且耗时）。

License
- 本仓库使用 MIT 许可证（见 `LICENSE` 文件）。

如果你希望我把 README 提交到仓库（git commit），或者要我把 README 内容做得更详细（添加 API、类说明、截图），告诉我下一步即可。