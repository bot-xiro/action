// PlayCore: 纯 GStreamer 播放管道层 (不依赖 JQ/quickjs)
// 供 gstplayer JSAPI 模块和 gstplayerd 独立守护进程共用
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

struct _GstElement;
struct _GstBus;
struct _GstPad;
typedef struct _GstElement GstElement;
typedef struct _GstBus GstBus;
typedef struct _GstPad GstPad;
typedef struct _GstPadProbeInfo GstPadProbeInfo;

namespace gstplayer {

// 设备几何 (youdao-rk3562-melon profile):
// 面板 DSI-1 480x960, Falcon 逻辑 960x266, direction 270, KMS plane 76.
// 逻辑->物理映射以真机触控映射为锚点 (见 references 设备 COS 手册附录B):
//   touchX = displayY + 107,  touchY = 959 - displayX
// 即逻辑屏幕是物理面板上 480x960 旋转 270 后、纵向裁出 266 的一条带 (居中于 y=107..372).
void gplayerLogicToPhys(int lx, int ly, int lw, int lh, int& px, int& py, int& pw, int& ph);

// kmssink render-rectangle 在本设备固件上是 GST_TYPE_ARRAY of gint (write-only),
// 不能按 boxed/字符串传, 需 GValue array 设置
void setRenderRect(GstElement* sink, int x, int y, int w, int h);

class PlayCore {
public:
    // state 事件: "opening" "play" "pause" "ready" "eos" "closed" "error: ..."
    typedef std::function<void(const std::string&)> EventFn;

    PlayCore();
    ~PlayCore();

    void setEventCallback(EventFn fn) { m_eventFn = fn; }

    // uri 支持 file:///path 与 http(s):// (B 站 CDN 会自动带 UA/Referer)
    bool open(const std::string& uri, const std::string& rect);
    void start();
    void pause();
    void seekMs(double ms);
    double positionMs();
    double durationMs();
    void close();  // 幂等
    // 更新视频显示区域 (逻辑坐标 "x,y,w,h"), 已有分辨率时立即按宽高比拟合
    void setRect(const std::string& rect);

private:
    static void onDemuxPadAdded(GstElement* demux, GstPad* pad, void* userData);
    static void onAudioDecodePadAdded(GstElement* decode, GstPad* pad, void* userData);
    static unsigned int capsProbe(GstPad* pad, GstPadProbeInfo* info, void* userData); // GstPadProbeReturn

    bool buildPipeline(const std::string& uri, const std::string& rect);
    bool linkVideoBranch(GstPad* demuxPad);
    bool linkAudioBranch(GstPad* demuxPad);
    void fitVideoRect(int vw, int vh);
    void busLoop();
    void emit(const std::string& s);
    void teardownLocked();

    std::mutex m_lock;
    GstElement* m_pipeline;
    GstBus* m_bus;
    std::thread m_busThread;
    std::atomic<bool> m_running;
    bool m_videoLinked;
    bool m_audioLinked;
    GstElement* m_kmsSink;    // 弱引用, 生命周期属于 m_pipeline
    GstElement* m_audioTail[4];
    std::string m_rectStr;
    int m_videoW;             // 最近一次解码器报告的源分辨率 (供 setRect 重新拟合)
    int m_videoH;
    EventFn m_eventFn;
};

}  // namespace gstplayer
