// 播放器业务类：内嵌 GStreamer 控制（dlopen 动态加载设备运行时库）。
// 纯 OS/C++ 实现，不依赖 jsutil，便于 host 单测。
//
// 设计说明：
//   - 设备只有 GStreamer 1.22.0 运行时库（libgstreamer-1.0.so.0.2200.0），无开发头文件。
//   - 因此通过 dlopen + dlsym 手写稳定 ABI 的函数指针绑定，编译零外部依赖，
//     运行时与设备库版本天然匹配，支持 seek / pause / resume / position / duration。
#pragma once
#include <string>
#include <memory>
#include <functional>

// 播放器稳定状态枚举（跨 JS 层协议）
enum class PlayerState {
    Idle = 0,      // 无 pipeline / NULL
    Loading = 1,   // 建立 pipeline 中
    Playing = 2,   // PLAYING
    Paused = 3,    // PAUSED
    Error = 4,
};

struct PlayerStatus {
    PlayerState state = PlayerState::Idle;
    long long positionMs = 0;   // 当前播放位置（毫秒）
    long long durationMs = 0;   // 总时长（毫秒，直播/未知为 -1）
    std::string lastError;
    std::string title;
};

class PlayerPipeline
{
public:
    PlayerPipeline();
    ~PlayerPipeline();

    // 参数校验（同步、纯函数）
    static bool validateUrl(const std::string &url);
    static bool validateType(const std::string &type);
    // 直播流（无 seek、无确定 duration）；ts 流视为直播
    static bool isLive(const std::string &type);

    // 加载（parse_launch 建立 pipeline 并预滚到 PAUSED），失败返回 false 并写 lastError
    bool load(const std::string &url, const std::string &type,
              const std::function<void(const std::string &json)> &onEvent,
              PlayerStatus &outStatus);

    bool play(PlayerStatus &outStatus);
    bool pause(PlayerStatus &outStatus);
    bool resume(PlayerStatus &outStatus);
    bool stop(PlayerStatus &outStatus);
    bool seek(double seconds, PlayerStatus &outStatus);
    bool refresh(PlayerStatus &outStatus);   // 查询 position/duration

    PlayerStatus getStatus() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};