// PlayCore 实现: GStreamer + MPP 硬解 + waylandsink (Weston 合成)
//
// 设备契约 (youdao-rk3562-melon profile):
//   - 屏幕合成走 Weston: Falcon UI 是 Weston top-level surface; kmssink
//     走 KMS plane 76 (Esmart1-win0), 会在 VOP2 硬件层盖住 UI (同 zpos
//     按 plane id 定序, 实测 2026-08-30), 无法用 plane-properties 压下去.
//   - 要让 UI 浮在视频上方, 视频必须进 Weston 的 client surface (waylandsink),
//     由 Weston 按 surface 顺序把 UI 画在视频上方; UI 画面要用 alpha 透明
//     让视频透出 (<hole>/透明背景).
//   - 面板 DSI-1 480x960, Falcon 逻辑 960x266, direction=270, 视频内容需
//     videoflip 90l (method=3) 补偿 direction 旋转.
//   - 解码 mppvideodec (rank 257), B 站 durl 为 MP4 (h264/aac)
//   - souphttpsrc 需 UA + Referer 才能直连 B 站 CDN
//   - waylandsink 需要 WAYLAND_DISPLAY, 由 daemon.cpp 启动时 setenv 兜底.
#include "PlayCore.h"

#include <gst/gst.h>
#include <syslog.h>
#include <cstdio>
#include <cstring>

// 日志: syslog (设备侧可能没开 syslog 转发) + /tmp/gstplayerd.log 留档,
// 便于查 probe/audio/state 问题 (媒体调试参考 skill: media-kms.md "独立日志").
#define GP_LOG(fmt, ...) do { \
    syslog(LOG_ERR, "[gstplayer] " fmt, ##__VA_ARGS__); \
    FILE* _lf = fopen("/tmp/gstplayerd.log", "a"); \
    if (_lf) { \
        fprintf(_lf, "[gstplayer] " fmt "\n", ##__VA_ARGS__); \
        fclose(_lf); \
    } \
} while (0)

namespace gstplayer {

static const int PANEL_W = 480;   // DRM mode: 480x960
static const int PANEL_H = 960;
static const int LOGIC_W = 960;
static const int LOGIC_H = 266;
static const int KMS_PLANE_ID = 76;

// 逻辑坐标 (x,y,w,h) -> 物理像素矩形; direction=270 时逻辑轴与物理轴互换
void gplayerLogicToPhys(int lx, int ly, int lw, int lh, int& px, int& py, int& pw, int& ph)
{
    // 逻辑 960x266 横屏 → 物理 480x960 竖屏 (direction=270).
    // 真机锚点 (references/设备ADB手册 附录B):
    //   触控坐标  touchX = logicalY + 107  (YOFFSET=107: 266 行居中于物理 480 行)
    //             touchY = 959 - logicalX
    // 视频 content 需让 KMS 看到的矩形落在同一坐标系上:
    //   面板横轴(物理x) ← 逻辑竖轴(逻辑y), 加 107 偏移;
    //   面板竖轴(物理y) ← 逻辑横轴(逻辑x), 镜像翻转.
    // 顶点映射 (0,0)→(107, 959), (960,266)→(373, 0).
    const int YOFF = 107;
    px = ly + YOFF;            // 逻辑 y → 物理 x (同向)
    py = (PANEL_H - 1) - (lx + lw - 1);  // 逻辑 x 右端 → 物理 y 顶端
    pw = lh;
    ph = lw;
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    if (pw <= 0) pw = 1;
    if (ph <= 0) ph = 1;
    if (px + pw > PANEL_W) pw = PANEL_W - px;
    if (py + ph > PANEL_H) ph = PANEL_H - py;
}

void setRenderRect(GstElement* sink, int x, int y, int w, int h)
{
    GValue arr = G_VALUE_INIT;
    GValue v = G_VALUE_INIT;
    g_value_init(&arr, GST_TYPE_ARRAY);
    g_value_init(&v, G_TYPE_INT);
    g_value_set_int(&v, x); gst_value_array_append_value(&arr, &v);
    g_value_set_int(&v, y); gst_value_array_append_value(&arr, &v);
    g_value_set_int(&v, w); gst_value_array_append_value(&arr, &v);
    g_value_set_int(&v, h); gst_value_array_append_value(&arr, &v);
    g_object_set_property(G_OBJECT(sink), "render-rectangle", &arr);
    g_value_unset(&v);
    g_value_unset(&arr);
}

static bool parseRect(const std::string& s, int* r)
{
    return sscanf(s.c_str(), "%d,%d,%d,%d", &r[0], &r[1], &r[2], &r[3]) == 4;
}

static void ensureGstInit()
{
    static std::once_flag once;
    std::call_once(once, [] {
        GP_LOG("gst_init enter");
        gst_init(NULL, NULL);
        GP_LOG("gst_init done");
    });
}

PlayCore::PlayCore()
    : m_pipeline(NULL), m_bus(NULL), m_running(false),
      m_videoLinked(false), m_audioLinked(false), m_kmsSink(NULL)
{
    memset(m_audioTail, 0, sizeof(m_audioTail));
}

PlayCore::~PlayCore() { close(); }

void PlayCore::emit(const std::string& s)
{
    EventFn fn = m_eventFn;
    if (fn) {
        try { fn(s); } catch (...) {}
    }
}

bool PlayCore::open(const std::string& uri, const std::string& rect)
{
    if (uri.empty() || uri.size() > 2048 ||
        uri.find_first_of("\r\n\t") != std::string::npos) {
        emit("error: bad uri");
        return false;
    }
    GP_LOG("open uri=%s rect=%s", uri.c_str(), rect.c_str());
    std::lock_guard<std::mutex> lock(m_lock);
    teardownLocked();
    ensureGstInit();
    if (!buildPipeline(uri, rect)) {
        emit("error: pipeline build failed");
        teardownLocked();
        return false;
    }
    emit("opening");
    return true;
}

void PlayCore::start()
{
    std::lock_guard<std::mutex> lock(m_lock);
    if (!m_pipeline) return;
    GP_LOG("start");
    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    emit("play");
}

void PlayCore::pause()
{
    std::lock_guard<std::mutex> lock(m_lock);
    if (!m_pipeline) return;
    GP_LOG("pause");
    gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
    emit("pause");
}

void PlayCore::seekMs(double ms)
{
    std::lock_guard<std::mutex> lock(m_lock);
    if (!m_pipeline) return;
    if (ms < 0) ms = 0;
    GP_LOG("seek %.0f ms", ms);
    gboolean ok = gst_element_seek_simple(m_pipeline,
        GST_FORMAT_TIME,
        (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
        (gint64)(ms * 1000000.0));
    GP_LOG("seek ok=%d", ok);
}

double PlayCore::positionMs()
{
    std::lock_guard<std::mutex> lock(m_lock);
    gint64 ns = 0;
    if (m_pipeline && !gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &ns)) ns = 0;
    return ns > 0 ? (double)(ns / 1000000) : 0.0;
}

double PlayCore::durationMs()
{
    std::lock_guard<std::mutex> lock(m_lock);
    gint64 ns = -1;
    if (m_pipeline) gst_element_query_duration(m_pipeline, GST_FORMAT_TIME, &ns);
    return ns > 0 ? (double)(ns / 1000000) : 0.0;
}

void PlayCore::close()
{
    std::lock_guard<std::mutex> lock(m_lock);
    teardownLocked();
    emit("closed");
}

void PlayCore::teardownLocked()
{
    GP_LOG("teardown enter");
    m_running = false;
    if (m_busThread.joinable() && std::this_thread::get_id() != m_busThread.get_id()) {
        m_busThread.join();
    }
    if (m_pipeline) gst_element_set_state(m_pipeline, GST_STATE_NULL);
    if (m_bus) { gst_object_unref(m_bus); m_bus = NULL; }
    if (m_pipeline) { gst_object_unref(m_pipeline); m_pipeline = NULL; }
    m_videoLinked = false;
    m_audioLinked = false;
    m_kmsSink = NULL;
    memset(m_audioTail, 0, sizeof(m_audioTail));
    GP_LOG("teardown done");
}

void PlayCore::onDemuxPadAdded(GstElement* demux, GstPad* pad, void* userData)
{
    PlayCore* self = static_cast<PlayCore*>(userData);
    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (!caps) caps = gst_pad_query_caps(pad, NULL);
    if (!caps) {
        GP_LOG("pad-added: no caps");
        return;
    }
    const gchar* mime = gst_structure_get_name(gst_caps_get_structure(caps, 0));
    std::string name = mime ? mime : "";
    GP_LOG("pad-added: %s", name.c_str());
    if (name.rfind("video/", 0) == 0) self->linkVideoBranch(pad);
    else if (name.rfind("audio/", 0) == 0) self->linkAudioBranch(pad);
    gst_caps_unref(caps);
}

bool PlayCore::linkVideoBranch(GstPad* demuxPad)
{
    if (m_videoLinked) return true;
    GstElement* queueV = gst_element_factory_make("queue", "vqueue");
    GstElement* parse = gst_element_factory_make("h264parse", "vparse");
    GstElement* dec = gst_element_factory_make("mppvideodec", "vdec");
    GstElement* flip = gst_element_factory_make("videoflip", "vflip");
    GstElement* conv = gst_element_factory_make("videoconvert", "vconv");
    if (!queueV || !parse || !dec || !flip || !conv) {
        GP_LOG("video branch factory failed q=%d p=%d d=%d f=%d c=%d",
               !!queueV, !!parse, !!dec, !!flip, !!conv);
        return false;
    }
    // gst-1.x videoflip method 枚举: 0 identity, 1 90r, 2 180, 3 90l, 4 horiz, 5 vert ...
    // 之前用 4 (横向镜像, 不旋转) 是错项: 视频只能按面板原生方向 (竖屏) 看.
    // 竖屏面板逻辑->物理映射 (px=ly+107, py=959-lx) 要求内容旋转 90l (逆时针):
    //   画面 up 指向 -phys_x (UI 正上方).
    g_object_set(G_OBJECT(flip), "method", 3 /* 90l counter-clockwise */, NULL);
    // 实测 (2026-08-30): kmssink 的 plane 76 (Esmart1) 始终压在 UI 平面 (Esmart0) 上,
    // plane-properties zpos=0 也压不下去 (VOP2 同 zpos 按 plane-id 定序).
    // 悬浮方案改用 Weston 合成: 视频走 waylandsink 作为 client surface,
    // UI surface 盖在上面, 透明区透出视频 (references/transparent.md + hole).
    GstElement* scale = gst_element_factory_make("videoscale", "vscale");
    GstElement* capsf = gst_element_factory_make("capsfilter", "vcaps");
    GstElement* sink = gst_element_factory_make("waylandsink", "vsink");
    if (!scale || !capsf || !sink) {
        GP_LOG("wayland branch factory failed s=%d cf=%d k=%d", !!scale, !!capsf, !!sink);
        gst_bin_remove_many(GST_BIN(m_pipeline), queueV, parse, dec, flip, conv, NULL);
        return false;
    }
    // 旋转后帧是竖向(96… ; direction=270), 缩放到面板物理尺寸, fullscreen 显示.
    GstCaps* sz = gst_caps_new_simple("video/x-raw",
        "width", G_TYPE_INT, PANEL_W,
        "height", G_TYPE_INT, PANEL_H,
        NULL);
    g_object_set(capsf, "caps", sz, NULL);
    gst_caps_unref(sz);
    g_object_set(sink, "fullscreen", TRUE, NULL);
    gst_bin_add_many(GST_BIN(m_pipeline), queueV, parse, dec, flip, conv, scale, capsf, sink, NULL);
    if (!gst_element_link_many(queueV, parse, dec, flip, conv, scale, capsf, sink, NULL)) {
        GP_LOG("video branch link failed (waylandsink path)");
        gst_bin_remove_many(GST_BIN(m_pipeline), queueV, parse, dec, flip, conv, scale, capsf, sink, NULL);
        return false;
    }
    GstPad* sinkPad = gst_element_get_static_pad(queueV, "sink");
    GstPadLinkReturn ret = gst_pad_link(demuxPad, sinkPad);
    gst_object_unref(sinkPad);
    if (ret != GST_PAD_LINK_OK) {
        GP_LOG("video pad link failed ret=%d", ret);
        gst_bin_remove_many(GST_BIN(m_pipeline), queueV, parse, dec, flip, conv, scale, capsf, sink, NULL);
        return false;
    }
    gst_element_sync_state_with_parent(queueV);
    gst_element_sync_state_with_parent(parse);
    gst_element_sync_state_with_parent(dec);
    gst_element_sync_state_with_parent(flip);
    gst_element_sync_state_with_parent(conv);
    gst_element_sync_state_with_parent(scale);
    gst_element_sync_state_with_parent(capsf);
    gst_element_sync_state_with_parent(sink);
    m_kmsSink = sink;
    GstPad* decSrc = gst_element_get_static_pad(dec, "src");
    if (decSrc) {
        gst_pad_add_probe(decSrc, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                          (GstPadProbeCallback)&PlayCore::capsProbe, this, NULL);
        gst_object_unref(decSrc);
    }
    m_videoLinked = true;
    GP_LOG("video branch linked");
    return true;
}

// 解码器 src caps 事件: 得到视频真实分辨率, 按宽高比居中
unsigned int PlayCore::capsProbe(GstPad*, GstPadProbeInfo* pi, void* userData)
{
    GstEvent* ev = GST_PAD_PROBE_INFO_EVENT(pi);
    if (!ev || GST_EVENT_TYPE(ev) != GST_EVENT_CAPS) return GST_PAD_PROBE_OK;
    GstCaps* caps = NULL;
    gst_event_parse_caps(ev, &caps);
    if (!caps || !gst_caps_is_fixed(caps)) return GST_PAD_PROBE_OK;
    GstStructure* st = gst_caps_get_structure(caps, 0);
    int w = 0, h = 0;
    if (gst_structure_get_int(st, "width", &w) && gst_structure_get_int(st, "height", &h) &&
        w > 0 && h > 0) {
        PlayCore* self = static_cast<PlayCore*>(userData);
        self->m_videoW = w; self->m_videoH = h;
        self->fitVideoRect(w, h);
    }
    return GST_PAD_PROBE_OK;
}

// 运行时切换显示区域 (控制条显隐驱动窗口/全屏切换). 立即按已知分辨率拟合.
void PlayCore::setRect(const std::string& rect)
{
    // rect 只接受 4 个整数的白名单格式
    int r[4];
    if (!parseRect(rect, r)) {
        GP_LOG("setRect invalid: %s", rect.c_str());
        return;
    }
    int vw, vh;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        m_rectStr = rect;
        vw = m_videoW; vh = m_videoH;
    }
    GP_LOG("setRect %s (video %dx%d)", rect.c_str(), vw, vh);
    // fitVideoRect 内部自取 m_lock, 这里不能持有锁再调
    if (vw > 0 && vh > 0) fitVideoRect(vw, vh);
}

void PlayCore::fitVideoRect(int vw, int vh)
{
    std::lock_guard<std::mutex> lock(m_lock);
    if (!m_kmsSink) return;
    int lx = 0, ly = 0, lw = LOGIC_W, lh = LOGIC_H;
    int r[4];
    if (parseRect(m_rectStr, r)) { lx = r[0]; ly = r[1]; lw = r[2]; lh = r[3]; }
    double ar = (double)vw / (double)vh;
    int fw = lw, fh = (int)(lw / ar + 0.5);
    if (fh > lh) { fh = lh; fw = (int)(lh * ar + 0.5); }
    int rx = lx + (lw - fw) / 2, ry = ly + (lh - fh) / 2;
    int px, py, pw, ph;
    gplayerLogicToPhys(rx, ry, fw, fh, px, py, pw, ph);
    setRenderRect(m_kmsSink, px, py, pw, ph);
    GP_LOG("fit rect video=%dx%d logic(%d,%d,%d,%d) phys(%d,%d,%d,%d)",
           vw, vh, rx, ry, fw, fh, px, py, pw, ph);
}

bool PlayCore::linkAudioBranch(GstPad* demuxPad)
{
    if (m_audioLinked) return true;
    GstElement* queueA = gst_element_factory_make("queue", "aqueue");
    GstElement* decode = gst_element_factory_make("decodebin", "adecode");
    GstElement* convert = gst_element_factory_make("audioconvert", "aconv");
    GstElement* resample = gst_element_factory_make("audioresample", "aresample");
    GstElement* volume = gst_element_factory_make("volume", "avol");
    GstElement* sink = gst_element_factory_make("alsasink", "asink");
    if (!queueA || !decode || !convert || !resample || !volume || !sink) {
        GP_LOG("audio branch factory failed q=%d d=%d c=%d r=%d v=%d s=%d",
               !!queueA, !!decode, !!convert, !!resample, !!volume, !!sink);
        return false;
    }
    gst_bin_add_many(GST_BIN(m_pipeline), queueA, decode, convert, resample, volume, sink, NULL);
    if (!gst_element_link_many(convert, resample, volume, sink, NULL)) {
        GP_LOG("audio tail link failed");
        gst_bin_remove_many(GST_BIN(m_pipeline), queueA, decode, convert, resample, volume, sink, NULL);
        return false;
    }
    g_signal_connect(decode, "pad-added", G_CALLBACK(&PlayCore::onAudioDecodePadAdded), this);
    GstPad* sinkPad = gst_element_get_static_pad(queueA, "sink");
    GstPadLinkReturn ret = gst_pad_link(demuxPad, sinkPad);
    gst_object_unref(sinkPad);
    if (ret != GST_PAD_LINK_OK) {
        GP_LOG("audio pad link failed ret=%d", ret);
        gst_bin_remove_many(GST_BIN(m_pipeline), queueA, decode, convert, resample, volume, sink, NULL);
        return false;
    }
    gst_element_sync_state_with_parent(queueA);
    gst_element_sync_state_with_parent(decode);
    m_audioLinked = true;
    m_audioTail[0] = convert; m_audioTail[1] = resample; m_audioTail[2] = volume; m_audioTail[3] = sink;
    GP_LOG("audio branch linked");
    return true;
}

void PlayCore::onAudioDecodePadAdded(GstElement* decode, GstPad* pad, void* userData)
{
    PlayCore* self = static_cast<PlayCore*>(userData);
    GstElement* convert = self->m_audioTail[0];
    if (!convert) return;
    GstPad* sinkPad = gst_element_get_static_pad(convert, "sink");
    if (sinkPad && !gst_pad_is_linked(sinkPad)) {
        if (gst_pad_link(pad, sinkPad) == GST_PAD_LINK_OK) {
            GP_LOG("audio decodebin pad linked");
            for (int i = 0; i < 4; i++) {
                if (self->m_audioTail[i]) gst_element_sync_state_with_parent(self->m_audioTail[i]);
            }
        } else {
            GP_LOG("audio decodebin pad link failed");
        }
    }
    if (sinkPad) gst_object_unref(sinkPad);
}

bool PlayCore::buildPipeline(const std::string& uri, const std::string& rect)
{
    m_pipeline = gst_pipeline_new("gstplayer-pipeline");
    if (!m_pipeline) {
        GP_LOG("pipeline factory failed");
        return false;
    }
    m_rectStr = rect;
    m_videoLinked = false;
    m_audioLinked = false;
    m_kmsSink = NULL;

    GstElement* src = NULL;
    GstElement* queue = gst_element_factory_make("queue", "demuxqueue");
    GstElement* demux = gst_element_factory_make("qtdemux", "demux");
    if (uri.rfind("file://", 0) == 0) {
        src = gst_element_factory_make("filesrc", "src");
        if (src) g_object_set(G_OBJECT(src), "location", uri.c_str() + 7, NULL);
    } else {
        src = gst_element_factory_make("souphttpsrc", "src");
        if (src) {
            g_object_set(G_OBJECT(src),
                "location", uri.c_str(),
                "timeout", (guint)15,
                "retries", (gint)0,
                NULL);
            // B 站 CDN 需浏览器 UA + Referer 防盗链
            GstStructure* hdrs = gst_structure_new("request-headers",
                "User-Agent", G_TYPE_STRING,
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
                "Referer", G_TYPE_STRING, "https://www.bilibili.com",
                NULL);
            g_object_set(G_OBJECT(src), "extra-headers", hdrs, NULL);
            gst_structure_free(hdrs);
            GP_LOG("souphttpsrc created, UA+Referer set");
        }
    }
    if (!src || !queue || !demux) {
        GP_LOG("factory failed src=%d queue=%d demux=%d", !!src, !!queue, !!demux);
        gst_object_unref(m_pipeline);
        m_pipeline = NULL;
        return false;
    }
    gst_bin_add_many(GST_BIN(m_pipeline), src, queue, demux, NULL);
    if (!gst_element_link_many(src, queue, demux, NULL)) {
        GP_LOG("src->queue->demux link failed");
        gst_object_unref(m_pipeline);
        m_pipeline = NULL;
        return false;
    }
    g_signal_connect(demux, "pad-added", G_CALLBACK(&PlayCore::onDemuxPadAdded), this);

    m_bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipeline));
    m_running = true;
    m_busThread = std::thread(&PlayCore::busLoop, this);
    return true;
}

void PlayCore::busLoop()
{
    GP_LOG("bus thread started");
    while (m_running) {
        GstBus* bus = m_bus;
        if (!bus) break;
        GstMessage* msg = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND,
            (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_WARNING
                             | GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_DURATION_CHANGED));
        if (!msg) continue;
        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            GP_LOG("bus EOS");
            emit("eos");
            break;
        case GST_MESSAGE_ERROR: {
            GError* err = NULL;
            gchar* dbg = NULL;
            gst_message_parse_error(msg, &err, &dbg);
            GP_LOG("bus ERROR: %s (%s)", err ? err->message : "?", dbg ? dbg : "");
            std::string s = "error: ";
            s += err ? err->message : "unknown";
            emit(s);
            if (err) g_error_free(err);
            if (dbg) g_free(dbg);
            break;
        }
        case GST_MESSAGE_DURATION_CHANGED:
            break;
        case GST_MESSAGE_ASYNC_DONE:
            GP_LOG("bus ASYNC_DONE");
            emit("ready");
            break;
        case GST_MESSAGE_WARNING: {
            GError* warn = NULL;
            gst_message_parse_warning(msg, &warn, NULL);
            GP_LOG("bus WARN: %s", warn ? warn->message : "?");
            if (warn) g_error_free(warn);
            break;
        }
        default:
            break;
        }
        gst_message_unref(msg);
    }
    GP_LOG("busLoop exit");
}

}  // namespace gstplayer
