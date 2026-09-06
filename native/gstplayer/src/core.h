// core.h: 新一代播放核心 (v2 全重写)
//
// 与旧实现 (PlayCore) 的本质区别:
//   1. 视频几何在 Weston 全局横屏空间 (960x480) 表达, 不做任何像素旋转.
//      本机 UI 带 y∈[107,373) (cfg yoffset=107), 视频面在 UI 之下, 由
//      Falcon 页面的 <hole> 透明区透出; 旋转由 Weston 输出 transform 完成.
//   2. 自动适配视频尺寸: caps 探针拿到视频分辨率后, 等比拟合进 UI 带,
//      运行时设置 waylandsink render-rectangle (自动信箱模式).
//   3. 音频走 decodebin (AAC→faad / MP3→mpg123audiodec 均由设备插件自动承接),
//      消除旧代码 "queueA→decodebin 静态 link_many" 的断链失声问题.
#ifndef GSTP_CORE_H
#define GSTP_CORE_H

#include <gst/gst.h>

#include <mutex>
#include <string>
#include <thread>

namespace gstplayer {

// 屏幕几何 (youdao-rk3562-melon profile, /etc/miniapp/resources/cfg.json)
static const int GLOBAL_W = 960;   // Weston 全局横屏宽
static const int GLOBAL_H = 480;   // Weston 全局横屏高
static const int UI_BAND_Y = 107;  // UI 带在全局空间中的 y 偏移
static const int UI_BAND_H = 266;  // UI 带高 (逻辑屏高)

class PlayCore {
public:
    using EventFn = void (*)(const std::string&, void*);
    // eventFn(eventLine, userData): 事件回调, eventLine 为 "S <state>" 等协议行
    // (不含行类型前缀的裸内容另见 daemon.cpp). 线程: gst 总线线程.
    void setEventCallback(EventFn fn, void* userData);

    // 打开流. rect: "auto" (默认, 等比拟合 UI 带) 或 "x,y,w,h" 全局坐标.
    bool open(const std::string& uri, const std::string& rect);
    void start();
    void pause();
    void seekMs(double ms);
    void close();
    // 查询; 失败返回 0
    double positionMs();
    double durationMs();
    // 视频分辨率 (解码输出, 旋转前); 未知返回 0
    int videoWidth() const { return m_videoW; }
    int videoHeight() const { return m_videoH; }

private:
    bool buildPipeline(const std::string& uri);
    void teardown();
    void emit(const std::string& state);
    void applyRectLocked();
    static void fitRect(int vw, int vh, const int rectIn[4], int out[4]);

    static void onDemuxPadAdded(GstElement* demux, GstPad* pad, void* self);
    static void onAudioDecodePadAdded(GstElement* decodebin, GstPad* pad, void* self);
    static GstPadProbeReturn capsProbe(GstPad* pad, GstPadProbeInfo* info, void* self);
    void busLoop();

    std::mutex m_lock;
    GstElement* m_pipeline = nullptr;
    GstBus* m_bus = nullptr;
    std::thread m_busThread;
    bool m_running = false;

    GstElement* m_sink = nullptr;      // waylandsink
    bool m_videoLinked = false;
    bool m_audioLinked = false;
    GstElement* m_audioConv = nullptr; // decodebin pad-added 的挂接点

    int m_rectIn[4] = {0, UI_BAND_Y, GLOBAL_W, UI_BAND_H}; // 宿主矩形 (全局坐标)
    bool m_rectAuto = true;
    int m_videoW = 0;
    int m_videoH = 0;

    EventFn m_eventFn = nullptr;
    void* m_eventData = nullptr;
};

}  // namespace gstplayer

#endif  // GSTP_CORE_H
