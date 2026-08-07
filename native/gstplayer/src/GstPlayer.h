#pragma once

#include "jqutil_v2/jqutil.h"

#include <atomic>
#include <string>
#include <thread>

// GStreamer 头文件（编译期由交叉环境提供，符号运行时从设备 libgstreamer 解析）
#include <gst/gst.h>

namespace gstplayer {

// 自研 gstplayer 原生模块（全新实现，不依赖系统 videoplayer）
//
// JS 侧使用（单例实例，无需 new）：
//   import { gstPlayer } from 'gstplayer'
//   gstPlayer.open({ uri, audio, pos_x, pos_y, pos_w, pos_h })
//   gstPlayer.start() / gstPlayer.pause() / gstPlayer.resume() / gstPlayer.close()
//   gstPlayer.stateChanged.on(function(state) { ... })   // "playing"/"paused"/"ended"/"error:..."
//
// 实现：手动构建 GStreamer 管线（souphttpsrc → queue → decodebin → 音视频分流）
//   - souphttpsrc: 直接创建，设置浏览器 UA + Referer（B站 CDN 防盗链必需；playbin 内部 source 无法获取）
//   - decodebin: pad-added 信号按媒体类型分流到 waylandsink / alsasink
//   - video-sink: waylandsink（weston 环境；kmssink 会死锁，禁用）
//   - audio-sink: alsasink（device=speaker）
//   - 状态/错误/EOS 通过 bus 轮询线程 → JQSignal 回调 JS 线程
class GstPlayer : public JQUTIL_NS::JQBaseObject {
public:
    GstPlayer();
    virtual ~GstPlayer();

    // ---- JS 方法（SetProtoMethod 绑定）----
    void open(JQUTIL_NS::JQFunctionInfo& info);
    void start(JQUTIL_NS::JQFunctionInfo& info);
    void pause(JQUTIL_NS::JQFunctionInfo& info);
    void resume(JQUTIL_NS::JQFunctionInfo& info);
    void close(JQUTIL_NS::JQFunctionInfo& info);

    // ---- JS 信号：gstPlayer.stateChanged.on(cb) / .off(cb) ----
    JQUTIL_NS::JQSignal<std::string> stateChanged;

private:
    bool buildPipeline(const std::string& uri, bool audio, const std::string& rect);
    void teardown();
    void busLoop();
    void emitState(const std::string& state);
    void onDecodebinPadAdded(GstPad* pad);

    static void decodebinPadAddedCb(GstElement* element, GstPad* pad, gpointer userdata);

    GstElement* pipeline_ = nullptr;     // gst_pipeline
    GstElement* decodebin_ = nullptr;    // decodebin（保留引用以便 teardown 前分流结束）
    GstElement* videoSink_ = nullptr;    // waylandsink
    GstElement* audioSink_ = nullptr;    // alsasink

    std::thread busThread_;
    std::atomic<bool> stopping_{false};
};

}  // namespace gstplayer
