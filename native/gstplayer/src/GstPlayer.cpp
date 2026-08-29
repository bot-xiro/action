// gstplayer: 播放器原生 JSAPI 模块 (GStreamer + MPP 硬解 + kmssink KMS 直出)
//
// JS 侧使用 (模块名为 "gstplayer", 导出单例 gstPlayer):
//   import { gstPlayer } from 'gstplayer'
//   gstPlayer.open(url, rect)      // rect: "x,y,w,h" 逻辑坐标, 缺省全屏
//   gstPlayer.start()  gstPlayer.pause()  gstPlayer.resume()  gstPlayer.close()
//   gstPlayer.seek(ms)             // 毫秒
//   gstPlayer.getPosition()        // -> 毫秒 number
//   gstPlayer.getDuration()        // -> 毫秒 number
//   gstPlayer.stateChanged.on(fn)  // 状态信号: "opening"/"ready"/"play"/"pause"/"eos"/"error: ..."
//
// 设备契约 (youdao-rk3562-melon profile):
//   - KMS: plane-id=76 (Esmart1-win0 overlay, zpos=2), driver rockchip,
//     不支持平面旋转 (仅 rotate-0/reflect-y) -> 用 videoflip 软转
//   - 面板 DSI-1 480x960, Falcon 逻辑 960x266, direction=270
//   - 解码 mppvideodec (rank 257), B 站 durl 为 MP4 (h264/aac)
//   - souphttpsrc 需 UA + Referer 才能直连 B 站 CDN
#include <gst/gst.h>
#include <syslog.h>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "jqutil_v2/jqutil.h"

using namespace JQUTIL_NS;

namespace gstplayer {

#define GP_LOG(fmt, ...) syslog(LOG_ERR, "[gstplayer] " fmt, ##__VA_ARGS__)

// ---- 设备几何 (来自 profile, 见 youdao-rk3562-x7.md) ----
static const int PANEL_W = 480;   // DRM mode #0: 480x960
static const int PANEL_H = 960;
static const int LOGIC_W = 960;   // cfg.json screen.width/height
static const int LOGIC_H = 266;
static const int KMS_PLANE_ID = 76;

// 逻辑坐标 (x,y,w,h) -> 物理像素矩形; direction=270 时逻辑轴与物理轴互换
// 已知事实: 逻辑宽 960 映射物理高 960; 逻辑高 266 映射物理宽 480
// (UI 由同一套 falcon 变换渲染, 与 touchscreen tp_direction=270 一致)
static void logicToPhys(int lx, int ly, int lw, int lh, int& px, int& py, int& pw, int& ph)
{
    // 横向缩放: 物理宽/逻辑高; 纵向缩放: 物理高/逻辑宽
    const double sw = (double)PANEL_W / LOGIC_H;
    const double sh = (double)PANEL_H / LOGIC_W;
    // direction=270 (逆时针 90): 逻辑原点左上 -> 物理右下
    // 推导: 逻辑 (0,0) 在物理 (PANEL_W,0) 附近; 逻辑 x 增大 -> 物理 y 增大
    // 逻辑 x∈[0,960] -> 物理 y∈[0,960]; 逻辑 y∈[0,266] -> 物理 x∈[480,0]
    px = (int)((LOGIC_H - (ly + lh)) * sw);
    py = (int)(lx * sh);
    pw = (int)(lh * sw);
    ph = (int)(lw * sh);
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    if (pw <= 0) pw = 1;
    if (ph <= 0) ph = 1;
    if (px + pw > PANEL_W) pw = PANEL_W - px;
    if (py + ph > PANEL_H) ph = PANEL_H - py;
}

class GstPlayer : public JQBaseObject {
public:
    // 导出到 JS 的状态信号: gstPlayer.stateChanged.on(fn)
    JQSignal<std::string> stateChanged;

    GstPlayer()
    {
        GP_LOG("GstPlayer ctr");
    }
    ~GstPlayer()
    {
        GP_LOG("GstPlayer dtr");
        teardown();
    }

    // open(uri[, rect])  rect="x,y,w,h" 逻辑坐标, 缺省全屏
    void open(JQFunctionInfo& info)
    {
        if (info.Length() < 1 || !JS_IsString(info[0])) {
            info.GetReturnValue().ThrowTypeError("open: uri required");
            return;
        }
        JSContext* ctx = info.GetContext();
        const char* c = JS_ToCString(ctx, info[0]);
        if (!c) {
            info.GetReturnValue().ThrowTypeError("open: invalid uri");
            return;
        }
        std::string uri(c);
        JS_FreeCString(ctx, c);
        if (uri.size() > 2048 || uri.find_first_of("\r\n\t") != std::string::npos) {
            info.GetReturnValue().ThrowTypeError("open: bad uri");
            return;
        }

        std::string rect = "0,0,960,266";
        if (info.Length() >= 2 && JS_IsString(info[1])) {
            const char* r = JS_ToCString(ctx, info[1]);
            if (r) { rect = r; JS_FreeCString(ctx, r); }
        }
        GP_LOG("open uri=%s rect=%s", uri.c_str(), rect.c_str());

        std::lock_guard<std::mutex> lock(m_lock);
        teardownLocked();
        if (!ensureGstInit()) {
            emitState("error: gst_init failed");
            return;
        }
        if (!buildPipeline(uri, rect)) {
            emitState("error: pipeline build failed");
            teardownLocked();
            return;
        }
        emitState("opening");
    }

    void start(JQFunctionInfo&)
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (!m_pipeline) return;
        GP_LOG("start");
        gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
        emitState("play");
    }

    void pause(JQFunctionInfo&)
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (!m_pipeline) return;
        GP_LOG("pause");
        gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
        emitState("pause");
    }

    void resume(JQFunctionInfo& info) { start(info); }

    void close(JQFunctionInfo&)
    {
        std::lock_guard<std::mutex> lock(m_lock);
        GP_LOG("close");
        teardownLocked();
        emitState("closed");
    }

    // seek(ms)
    void seek(JQFunctionInfo& info)
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (!m_pipeline) return;
        double ms = 0;
        if (info.Length() < 1 || !JS_IsNumber(info[0])) {
            info.GetReturnValue().ThrowTypeError("seek: ms required");
            return;
        }
        JS_ToFloat64(info.GetContext(), &ms, info[0]);
        if (ms < 0) ms = 0;
        gint64 pos = (gint64)(ms * 1000000.0);
        GP_LOG("seek %.0f ms", ms);
        gboolean ok = gst_element_seek_simple(m_pipeline,
            GST_FORMAT_TIME,
            (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
            pos);
        GP_LOG("seek ok=%d", ok);
    }

    void getPosition(JQFunctionInfo& info)
    {
        std::lock_guard<std::mutex> lock(m_lock);
        gint64 ns = 0;
        if (m_pipeline && !gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &ns)) ns = 0;
        if (ns < 0) ns = 0;
        info.GetReturnValue().Set((double)(ns / 1000000));
    }

    void getDuration(JQFunctionInfo& info)
    {
        std::lock_guard<std::mutex> lock(m_lock);
        gint64 ns = -1;
        if (m_pipeline) gst_element_query_duration(m_pipeline, GST_FORMAT_TIME, &ns);
        info.GetReturnValue().Set(ns > 0 ? (double)(ns / 1000000) : 0.0);
    }

private:
    // 解析 "x,y,w,h"
    static bool parseRect(const std::string& s, int* r)
    {
        int n = sscanf(s.c_str(), "%d,%d,%d,%d", &r[0], &r[1], &r[2], &r[3]);
        return n == 4;
    }

    static bool ensureGstInit()
    {
        static std::once_flag once;
        static bool ok = false;
        std::call_once(once, [] {
            GP_LOG("gst_init enter");
            gst_init(NULL, NULL);
            ok = true;
            GP_LOG("gst_init done");
        });
        return ok;
    }

    // qtdemux 动态 pad: 按 caps 前缀区分
    static void onDemuxPadAdded(GstElement* demux, GstPad* pad, gpointer userData)
    {
        GstPlayer* self = static_cast<GstPlayer*>(userData);
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

    bool linkVideoBranch(GstPad* demuxPad)
    {
        if (m_videoLinked) return true;  // 只接第一路视频
        GstElement* queueV = gst_element_factory_make("queue", "vqueue");
        GstElement* parse = gst_element_factory_make("h264parse", "vparse");
        GstElement* dec = gst_element_factory_make("mppvideodec", "vdec");
        GstElement* flip = gst_element_factory_make("videoflip", "vflip");
        GstElement* conv = gst_element_factory_make("videoconvert", "vconv");
        GstElement* queueV2 = gst_element_factory_make("queue", "vqueue2");
        GstElement* sink = gst_element_factory_make("kmssink", "vsink");
        if (!queueV || !parse || !dec || !flip || !conv || !queueV2 || !sink) {
            GP_LOG("video branch factory failed q=%d p=%d d=%d f=%d c=%d q2=%d s=%d",
                   !!queueV, !!parse, !!dec, !!flip, !!conv, !!queueV2, !!sink);
            return false;
        }
        // 屏幕竖屏: videoflip 顺时针 90°, 再 videoconvert, kmssink 显示
        g_object_set(G_OBJECT(flip), "method", 4 /* clockwise, GST_VIDEO_FLIP_METHOD_CLOCKWISE */, NULL);
        int lx = 0, ly = 0, lw = LOGIC_W, lh = LOGIC_H;
        if (!m_rectStr.empty()) {
            int r[4];
            if (parseRect(m_rectStr, r)) { lx = r[0]; ly = r[1]; lw = r[2]; lh = r[3]; }
        }
        int px, py, pw, ph;
        logicToPhys(lx, ly, lw, lh, px, py, pw, ph);
        GP_LOG("kmss rect LOGIC(%d,%d,%d,%d) -> PHYS(%d,%d,%d,%d)", lx, ly, lw, lh, px, py, pw, ph);
        gchar rectStr[64];
        snprintf(rectStr, sizeof(rectStr), "<%d,%d,%d,%d>", px, py, pw, ph);
        g_object_set(G_OBJECT(sink),
            "plane-id", (gint64)KMS_PLANE_ID,
            "render-rectangle", rectStr,
            "can-scale", TRUE,
            "sync", TRUE,
            NULL);
        gst_bin_add_many(GST_BIN(m_pipeline), queueV, parse, dec, flip, conv, queueV2, sink, NULL);
        if (!gst_element_link_many(queueV, parse, dec, flip, conv, queueV2, sink, NULL)) {
            GP_LOG("video branch link failed");
            gst_bin_remove_many(GST_BIN(m_pipeline), queueV, parse, dec, flip, conv, queueV2, sink, NULL);
            return false;
        }
        // 接到 qtdemux 的 dyn pad
        GstPad* sinkPad = gst_element_get_static_pad(queueV, "sink");
        GstPadLinkReturn ret = gst_pad_link(demuxPad, sinkPad);
        gst_object_unref(sinkPad);
        if (ret != GST_PAD_LINK_OK) {
            GP_LOG("video pad link failed ret=%d", ret);
            gst_bin_remove_many(GST_BIN(m_pipeline), queueV, parse, dec, flip, conv, queueV2, sink, NULL);
            return false;
        }
        gst_element_sync_state_with_parent(queueV);
        gst_element_sync_state_with_parent(parse);
        gst_element_sync_state_with_parent(dec);
        gst_element_sync_state_with_parent(flip);
        gst_element_sync_state_with_parent(conv);
        gst_element_sync_state_with_parent(queueV2);
        gst_element_sync_state_with_parent(sink);
        m_videoLinked = true;
        GP_LOG("video branch linked");
        return true;
    }

    // 音频: qtdemux pad -> queue -> decodebin -> audioconvert -> audioresample -> alsasink
    bool linkAudioBranch(GstPad* demuxPad)
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
        // decodebin 动态 pad -> audioconvert
        g_signal_connect(decode, "pad-added", G_CALLBACK(onAudioDecodePadAdded), this);
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

    static void onAudioDecodePadAdded(GstElement* decode, GstPad* pad, gpointer userData)
    {
        GstPlayer* self = static_cast<GstPlayer*>(userData);
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

    bool buildPipeline(const std::string& uri, const std::string& rect)
    {
        m_pipeline = gst_pipeline_new("gstplayer-pipeline");
        if (!m_pipeline) {
            GP_LOG("pipeline factory failed");
            return false;
        }
        m_rectStr = rect;
        m_videoLinked = false;
        m_audioLinked = false;
        memset(m_audioTail, 0, sizeof(m_audioTail));

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
            if (m_pipeline) { gst_object_unref(m_pipeline); m_pipeline = NULL; }
            return false;
        }
        gst_bin_add_many(GST_BIN(m_pipeline), src, queue, demux, NULL);
        if (!gst_element_link_many(src, queue, demux, NULL)) {
            GP_LOG("src->queue->demux link failed");
            gst_object_unref(m_pipeline);
            m_pipeline = NULL;
            return false;
        }
        g_signal_connect(demux, "pad-added", G_CALLBACK(onDemuxPadAdded), this);

        m_bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipeline));
        m_running = true;
        m_busThread = std::thread(&GstPlayer::busLoop, this);
        return true;
    }

    // 消息总线: 每 100ms 轮询, 跨线程事件经 JQSignal 投递回 JS 线程
    void busLoop()
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
                emitState("eos");
                break;
            case GST_MESSAGE_ERROR: {
                GError* err = NULL;
                gchar* dbg = NULL;
                gst_message_parse_error(msg, &err, &dbg);
                GP_LOG("bus ERROR: %s (%s)", err ? err->message : "?", dbg ? dbg : "");
                std::string s = "error: ";
                s += err ? err->message : "unknown";
                emitState(s);
                if (err) g_error_free(err);
                if (dbg) g_free(dbg);
                break;
            }
            case GST_MESSAGE_DURATION_CHANGED: {
                // base 已缓存, 提示 JS 重新查询时长
                emitState("duration");
                break;
            }
            case GST_MESSAGE_ASYNC_DONE:
                GP_LOG("bus ASYNC_DONE");
                emitState("ready");
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

    void emitState(const std::string& s)
    {
        try {
            stateChanged.emit(s);
        } catch (...) {}
    }

    void teardown()
    {
        std::lock_guard<std::mutex> lock(m_lock);
        teardownLocked();
    }

    void teardownLocked()
    {
        GP_LOG("teardown enter");
        m_running = false;
        if (m_busThread.joinable()) {
            if (std::this_thread::get_id() == m_busThread.get_id()) {
                // 不应自相 poll, 此类调用不会发生
            } else {
                m_busThread.join();
            }
        }
        if (m_pipeline) {
            gst_element_set_state(m_pipeline, GST_STATE_NULL);
        }
        if (m_bus) { gst_object_unref(m_bus); m_bus = NULL; }
        if (m_pipeline) { gst_object_unref(m_pipeline); m_pipeline = NULL; }
        m_videoLinked = false;
        m_audioLinked = false;
        memset(m_audioTail, 0, sizeof(m_audioTail));
        GP_LOG("teardown done");
    }

    std::mutex m_lock;
    GstElement* m_pipeline = NULL;
    GstBus* m_bus = NULL;
    std::thread m_busThread;
    std::atomic<bool> m_running{ false };
    bool m_videoLinked = false;
    bool m_audioLinked = false;
    GstElement* m_audioTail[4] = { NULL, NULL, NULL, NULL };
    std::string m_rectStr;
};

static JSValue createGstPlayer(JQModuleEnv* env)
{
    JQFunctionTemplateRef tpl = JQFunctionTemplate::New(env, "gstPlayer");
    tpl->InstanceTemplate()->setObjectCreator([]() {
        static GstPlayer* instance = []() {
            GstPlayer* p = new GstPlayer();
            p->REF();
            return p;
        }();
        return instance;
    });
    tpl->SetProtoMethod("open", &GstPlayer::open);
    tpl->SetProtoMethod("start", &GstPlayer::start);
    tpl->SetProtoMethod("pause", &GstPlayer::pause);
    tpl->SetProtoMethod("resume", &GstPlayer::resume);
    tpl->SetProtoMethod("close", &GstPlayer::close);
    tpl->SetProtoMethod("seek", &GstPlayer::seek);
    tpl->SetProtoMethod("getPosition", &GstPlayer::getPosition);
    tpl->SetProtoMethod("getDuration", &GstPlayer::getDuration);
    // 信号 (JS 侧 on/off): gstPlayer.stateChanged.on(fn)
    tpl->InstanceTemplate()->Set("stateChanged", &GstPlayer::stateChanged);
    return tpl->CallConstructor();
}

void gstplayer_init(JQModuleEnv* env)
{
    env->setModuleExport("gstPlayer", createGstPlayer(env));
}

}  // namespace gstplayer
