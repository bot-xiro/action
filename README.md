# PV Player 测试应用

有道词典笔（Youdao Dictionary Pen, P5/Melon Pro · RK3562）在线视频播放器可行性测试。

## 架构

- **Falcon UI 层**：Vue 2 SFC（指点手），展示状态、控制按钮、进度条、事件日志。
- **native JSAPI** `pvplayer`：
  - dlopen 动态加载设备 GStreamer 1.22.0 运行时（`libgstreamer-1.0.so.0`）。
  - `parse_launch` 建立 `souphttpsrc → hls/tsdemux/qtdemux → h264parse → mppvideodec → kmssink` 管线。
  - KMS 参数来自探测：`rockchip` / connector 125 / plane 54。
  - 暴露 `loadP/playP/pauseP/resumeP/stopP/seekP/refreshP` Promise 与 `pvevent` 状态事件。
- **在线测试视频**：内置四个公开 HLS/TS/MP4 源（见 `src/services/video-sources.js`）。

## 构建

GitHub Actions（分支 `pvplayer-test-v1`）交叉编译 + 打包 AMR。

设备端：
```sh
adb push pvplayer-test/dist/8001812345678903.amr /tmp/
adb shell miniapp_cli install /tmp/8001812345678903.amr
adb shell miniapp_cli start 8001812345678903 --player
```