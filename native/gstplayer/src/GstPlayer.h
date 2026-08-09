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

    // 进度条支持（返回值均为毫秒，double；seek 入参为毫秒）
    void getDuration(JQUTIL_NS::JQFunctionInfo& info);
    void getPosition(JQUTIL_NS::JQFunctionInfo& info);
    void seek(JQUTIL_NS::JQFunctionInfo& info);

    // ---- JS 信号：gstPlayer.stateChanged.on(cb) / .off(cb) ----
    JQUTIL_NS::JQSignal<std::string> stateChanged;

private:
    bool buildPipeline(const std::string& uri, bool audio, const std::string& rect, const std::string& fill);
    void teardown();
    void busLoop();
    void emitState(const std::string& state);
    void onQtdemuxPadAdded(GstPad* pad);          // qtdemux 动态 pad：video→显式硬解链 / audio→音频解码
    void onDecodebinPadAdded(GstPad* pad);        // 音频解码 decodebin 输出 → alsasink

    static void qtdemuxPadAddedCb(GstElement* element, GstPad* pad, gpointer userdata);
    static void decodebinPadAddedCb(GstElement* element, GstPad* pad, gpointer userdata);

    GstElement* pipeline_ = nullptr;     // gst_pipeline
    GstElement* demux_ = nullptr;        // qtdemux（mp4/m4s 解复用，B 站 CDN 均为 mp4 容器）
    GstElement* vparse_ = nullptr;       // h264parse（avcC → byte-stream，节帧边界）
    GstElement* vdec_ = nullptr;         // mppvideodec（RK MPP 硬解，rotation 硬件旋转，DMA-BUF 输出）
    GstElement* vqueue_ = nullptr;       // 视频缓冲 queue（静态后端入口；动态 pad（h264/fallback）统一接此）
    GstElement* vconvert_ = nullptr;     // videoconvert（格式协商缓冲：解码 DMA-BUF → sink 可接受格式）
    GstElement* decodebin_ = nullptr;    // 音频解码器（AAC → raw，输出接 audioconvert → alsasink）
    GstElement* aconvert_ = nullptr;     // audioconvert（音频格式协商，防 raw caps 与 alsasink 不匹配卡 preroll）
    GstElement* aresample_ = nullptr;    // audioresample（采样率协商，防 44.1k/48k 不匹配卡 preroll）
    GstElement* videoSink_ = nullptr;    // waylandsink
    GstElement* audioSink_ = nullptr;    // alsasink
    GstElement* videoFlip_ = nullptr;    // 遗留：videoflip 已淘汰（mppvideodec rotation 替代），保留指针置空兼容 teardown

    std::thread busThread_;
    std::atomic<bool> stopping_{false};
};

}  // namespace gstplayer
