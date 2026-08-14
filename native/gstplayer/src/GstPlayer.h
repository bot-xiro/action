#pragma once

#include "jqutil_v2/jqutil.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

// GStreamer 头文件（编译期由交叉环境提供，符号运行时从设备 libgstreamer 解析）
#include <gst/gst.h>

namespace gstplayer {

class ControlBar;

typedef struct _GdkPixbuf GdkPixbuf;

// 自研 gstplayer 原生模块（全新实现，不依赖系统 videoplayer）
//
// JS 侧使用（单例实例，无需 new）：
//   import { gstPlayer } from 'gstplayer'
//   gstPlayer.open({ uri, audio, pos_x, pos_y, pos_w, pos_h })
//   gstPlayer.start() / gstPlayer.pause() / gstPlayer.resume() / gstPlayer.close()
//   gstPlayer.stateChanged.on(function(state) { ... })   // "playing"/"paused"/"ended"/"error:..."
//   gstPlayer.setBarState({visible, playing, ended, error, position, duration})  // 悬浮控制栏
//
// 实现：手动构建 GStreamer 管线（souphttpsrc → queue → decodebin → 音视频分流）
//   - souphttpsrc: 直接创建，设置浏览器 UA + Referer（B站 CDN 防盗链必需；playbin 内部 source 无法获取）
//   - decodebin: pad-added 信号按媒体类型分流到 waylandsink / alsasink
//   - 视频链：qtdemux → h264parse → mppvideodec → videoconvert → videoscale(等比留边)
//     → capsfilter(画布尺寸) → gdkpixbufoverlay(悬浮控制栏) → videoconvert → sink
//   - video-sink: kmssink(KMSSINK_TEST) / waylandsink
//   - audio-sink: alsasink（device=speaker）
//   - 状态/错误/EOS 通过 bus 轮询线程 → JQSignal 回调 JS 线程
class GstPlayer : public JQUTIL_NS::JQBaseObject {
public:
    GstPlayer();
    virtual ~GstPlayer();

    // ---- JS 方法（SetProtoMethod 绑定）----
    void preheat(JQUTIL_NS::JQFunctionInfo& info);  // 播放路径外的一次性预热（UI 平面层级等）
    void open(JQUTIL_NS::JQFunctionInfo& info);
    void start(JQUTIL_NS::JQFunctionInfo& info);
    void pause(JQUTIL_NS::JQFunctionInfo& info);
    void resume(JQUTIL_NS::JQFunctionInfo& info);
    void close(JQUTIL_NS::JQFunctionInfo& info);

    // 进度条支持（返回值均为毫秒，double；seek 入参为毫秒）
    void getDuration(JQUTIL_NS::JQFunctionInfo& info);
    void getPosition(JQUTIL_NS::JQFunctionInfo& info);
    void seek(JQUTIL_NS::JQFunctionInfo& info);

    // 双指缩放支持：播放中动态更新渲染区域（逻辑坐标，内部换算物理 CRTC）
    void setRect(JQUTIL_NS::JQFunctionInfo& info);

    // 通用 HTTP GET：popen 调设备 curl，强制带浏览器 UA + Referer
    // （系统 http JSAPI 不发送自定义 header，B 站风控接口（搜索等）会返回
    //  v_voucher 空结果；curl 带 UA/Referer 实测可正常返回）
    void httpGet(JQUTIL_NS::JQFunctionInfo& info);

    // 【2026-08-11 动态层级】运行时切换视频 plane 76 zpos：
    // 播放中置顶(3)全屏可见 / 控制栏唤出时置底(0)让 UI 可操作（见 player.vue）
    void setVideoZpos(JQUTIL_NS::JQFunctionInfo& info);

    // 【2026-08-14 悬浮控制栏】更新叠加在视频帧上的控制栏状态并重绘
    // 入参: setBarState({visible:bool, playing:bool, ended:bool, error:bool,
    //                    position:ms, duration:ms})
    void setBarState(JQUTIL_NS::JQFunctionInfo& info);

    // ---- JS 信号：gstPlayer.stateChanged.on(cb) / .off(cb) ----
    JQUTIL_NS::JQSignal<std::string> stateChanged;

private:
    bool buildPipeline(const std::string& uri, bool audio, const std::string& rect,
                       const std::string& fill, int canvasW, int canvasH);
    void applyCanvasContent(int srcW, int srcH);   // 按源尺寸计算等比内容尺寸+黑边，设置 vcaps/vbox
    void teardown();
    void busLoop();
    void emitState(const std::string& state);
    void onQtdemuxPadAdded(GstPad* pad);          // qtdemux 动态 pad：video→显式硬解链 / audio→音频解码
    void onDecodebinPadAdded(GstPad* pad);        // 音频解码 decodebin 输出 → alsasink
    void refreshBar();                            // 重绘悬浮控制栏（状态由成员变量决定）
    void rebuildForSource();                      // 【竖屏支持 2026-08-14】按真实源尺寸重建管线

    static void qtdemuxPadAddedCb(GstElement* element, GstPad* pad, gpointer userdata);
    static void decodebinPadAddedCb(GstElement* element, GstPad* pad, gpointer userdata);

    // 重建支持（竖屏/非常规比例视频）：open 后首个 pad-added 若发现源尺寸与
    // 默认 16:9 不一致 → start() 前重建管线（caps 构建期固定，避免动态改
    // capsfilter caps 破坏 FLUSH seek）
    std::string rebuildUri_;
    bool rebuildAudio_ = true;
    std::string rebuildRect_;
    std::string rebuildFill_;
    int appliedSrcW_ = 1280;    // 当前 vcaps 按此源尺寸设置
    int appliedSrcH_ = 720;
    int rebuildSrcW_ = 0;       // pad-added 记录的真实源尺寸（重建用）
    int rebuildSrcH_ = 0;
    bool rebuildNeeded_ = false; // pad-added 请求重建
    bool rebuilding_ = false;    // 重建进行中（防 pad-added 重入）

    GstElement* pipeline_ = nullptr;     // gst_pipeline
    GstElement* demux_ = nullptr;        // qtdemux（mp4/m4s 解复用，B 站 CDN 均为 mp4 容器）
    GstElement* vparse_ = nullptr;       // h264parse（avcC → byte-stream，节帧边界）
    GstElement* vdec_ = nullptr;         // mppvideodec（RK MPP 硬解，rotation 硬件旋转，DMA-BUF 输出）
    GstElement* vqueue_ = nullptr;       // 视频缓冲 queue（静态后端入口；动态 pad（h264/fallback）统一接此）
    GstElement* vconvert_ = nullptr;     // videoconvert（格式协商缓冲：解码 DMA-BUF → sink 可接受格式）
    GstElement* vscale_ = nullptr;       // videoscale（等比缩放：源 → 内容尺寸，见 applyCanvasContent）
    GstElement* vcaps_ = nullptr;        // capsfilter（内容尺寸 video/x-raw,width=W,height=H —— 等比后尺寸）
    GstElement* vbox_ = nullptr;         // videobox（黑边补齐到画布尺寸 = sink render-rectangle）
    GstElement* voverlay_ = nullptr;     // gdkpixbufoverlay（悬浮控制栏合入视频帧）
    GstElement* vtitleoverlay_ = nullptr;// gdkpixbufoverlay（顶部标题条合入视频帧，2026-08-14）
    GstElement* vconvert2_ = nullptr;    // 第二 videoconvert（overlay 输出格式协商缓冲）
    GstElement* decodebin_ = nullptr;    // 音频解码器（AAC → raw，输出接 aconvert → aresample → acaps → avolume → alsasink）
    GstElement* aconvert_ = nullptr;     // audioconvert（音频格式协商，防 raw caps 与 alsasink 不匹配卡 preroll）
    GstElement* aresample_ = nullptr;    // audioresample（采样率协商，防 44.1k/48k 不匹配卡 preroll）
    GstElement* acaps_ = nullptr;        // capsfilter（强制 S16LE/44.1k/2ch，2026-08-15 修复声音沙沙）
    GstElement* avolume_ = nullptr;      // volume（音量控制，2026-08-15 重写）
    GstElement* videoSink_ = nullptr;    // kmssink / waylandsink
    GstElement* audioSink_ = nullptr;    // alsasink
    GstElement* videoFlip_ = nullptr;    // 遗留：videoflip 已淘汰（mppvideodec rotation 替代），保留指针置空兼容 teardown

    // 悬浮控制栏
    ControlBar* bar_ = nullptr;          // cairo 控制栏渲染器（底部控制栏）
    ControlBar* titleBar_ = nullptr;     // cairo 标题条渲染器（顶部标题，2026-08-14）
    GdkPixbuf* barPixbuf_ = nullptr;     // 当前叠加的 pixbuf（GdkPixbuf，需 unref）
    GdkPixbuf* titlePixbuf_ = nullptr;   // 标题条 pixbuf
    int canvasW_ = 0;                    // 画布尺寸（= sink render-rectangle 尺寸）
    int canvasH_ = 0;
    bool barVisible_ = true;
    bool barPlaying_ = false;
    bool barEnded_ = false;
    bool barError_ = false;
    bool barHiddenSet_ = false;    // ��藏时透明条带已设置（轮��防重复分���）
    bool seeking_ = false;         // FLUSH seek 进行中（防 getPosition 返回旧位置导致 UI "��回"）
    double seekTargetMs_ = 0.0;    // seek 目标位置（ms，seeking_=true 时返回此值）    // 隐藏时透明条带已设置（轮询防重复分配）
    double barPosMs_ = 0.0;
    double barDurMs_ = 0.0;
    std::string barTitle_;               // 视频标题（顶部标题条）

    std::thread busThread_;
    std::atomic<bool> stopping_{false};
    std::mutex barMutex_;        // 控制栏刷新互斥（JS 线程 setBarState 与 bus 线程状态联动）
};

}  // namespace gstplayer



