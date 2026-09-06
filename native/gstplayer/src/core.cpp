// core.cpp: 播放核心实现 (v2 全重写)
//
// 管线:
//   souphttpsrc(UA+Referer) ! queue ! qtdemux
//     video pad -> h264parse ! mppvideodec ! waylandsink (render-rectangle 自动适配)
//     audio pad -> queue ! decodebin ! audioconvert ! audioresample ! volume ! alsasink
//
// 几何契约 (youdao-rk3562-melon):
//   Weston 全局空间 960x480 横屏; Falcon UI 带 y∈[107,373).
//   视频以等比拟合矩形呈现在 UI 带内, 面在 UI 之下 (<hole> 透出).
//   render-rectangle 用全局坐标, 由 caps 探针在拿到分辨率后运行时设置.
//
// 日志: syslog + /tmp/gstplayerd.log 双写 (设备侧调试).
#include "core.h"

#include <syslog.h>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace gstplayer {

static long long nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

#define GP_LOG(fmt, ...) do { \
    syslog(LOG_ERR, "[gstplayer] " fmt, ##__VA_ARGS__); \
    FILE* _lf = fopen("/tmp/gstplayerd.log", "a"); \
    if (_lf) { \
        fprintf(_lf, "[gstplayer] " fmt "\n", ##__VA_ARGS__); \
        fclose(_lf); \
    } \
} while (0)

static void ensureGstInit()
{
    static bool inited = false;
    if (inited) return;
    GP_LOG("gst_init enter");
    gst_init(NULL, NULL);
    GP_LOG("gst_init done");
    inited = true;
}

void PlayCore::setEventCallback(EventFn fn, void* userData)
{
    m_eventFn = fn;
    m_eventData = userData;
}

void PlayCore::emit(const std::string& state)
{
    if (m_eventFn) {
        try { m_eventFn(state, m_eventData); } catch (...) {}
    }
}

// 等比拟合: 视频 vw x vh 拟合进 rectIn (全局坐标), 居中, 结果写 out.
// 纯函数, 四角/极端宽高比安全 (vh/vw 为 0 时退回宿主矩形).
void PlayCore::fitRect(int vw, int vh, const int rectIn[4], int out[4])
{
    out[0] = rectIn[0]; out[1] = rectIn[1];
    out[2] = rectIn[2]; out[3] = rectIn[3];
    if (vw <= 0 || vh <= 0 || rectIn[2] <= 0 || rectIn[3] <= 0) return;
    double ar = (double)vw / (double)vh;
    int fw = rectIn[2];
    int fh = (int)(fw / ar + 0.5);
    if (fh > rectIn[3]) {
        fh = rectIn[3];
        fw = (int)(fh * ar + 0.5);
    }
    if (fw < 1) fw = 1;
    if (fh < 1) fh = 1;
    out[0] = rectIn[0] + (rectIn[2] - fw) / 2;
    out[1] = rectIn[1] + (rectIn[3] - fh) / 2;
    out[2] = fw;
    out[3] = fh;
}

bool PlayCore::open(const std::string& uri, const std::string& rect)
{
    if (uri.empty() || uri.size() > 2048 ||
        uri.find_first_of("\r\n\t") != std::string::npos) {
        emit("error: bad uri");
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(m_lock);
        m_rectAuto = (rect.empty() || rect == "auto");
        if (!m_rectAuto) {
            int r[4];
            if (sscanf(rect.c_str(), "%d,%d,%d,%d", &r[0], &r[1], &r[2], &r[3]) != 4) {
                emit("error: bad rect");
                return false;
            }
            m_rectIn[0] = r[0]; m_rectIn[1] = r[1];
            m_rectIn[2] = r[2]; m_rectIn[3] = r[3];
        } else {
            m_rectIn[0] = 0; m_rectIn[1] = UI_BAND_Y;
            m_rectIn[2] = GLOBAL_W; m_rectIn[3] = UI_BAND_H;
        }
    }
    GP_LOG("open uri(96)=%.96s rect=%s", uri.c_str(), m_rectAuto ? "auto" : rect.c_str());

    teardown();
    ensureGstInit();
    if (!buildPipeline(uri)) {
        emit("error: pipeline build failed");
        teardown();
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
    // "play" 事件不再乐观发出: 由总线 STATE_CHANGED (管道真正到 PLAYING)
    // 触发, 页面在预滚/起播等待期间如实显示 "缓冲中…"
    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
}

void PlayCore::pause()
{
    std::lock_guard<std::mutex> lock(m_lock);
    if (!m_pipeline) return;
    GP_LOG("pause");
    // 同 start: "pause" 由总线 STATE_CHANGED 到 PAUSED 触发
    gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
}

void PlayCore::seekMs(double ms)
{
    std::lock_guard<std::mutex> lock(m_lock);
    if (!m_pipeline) return;
    if (ms < 0) ms = 0;
    GP_LOG("seek %.0f ms", ms);
    gboolean ok = gst_element_seek_simple(m_pipeline, GST_FORMAT_TIME,
        (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
        (gint64)(ms * GST_MSECOND));
    GP_LOG("seek ok=%d", (int)ok);
}

double PlayCore::positionMs()
{
    std::lock_guard<std::mutex> lock(m_lock);
    gint64 ns = 0;
    if (m_pipeline && !gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &ns)) ns = 0;
    return ns > 0 ? (double)(ns / GST_MSECOND) : 0.0;
}

double PlayCore::durationMs()
{
    std::lock_guard<std::mutex> lock(m_lock);
    gint64 ns = -1;
    if (m_pipeline) gst_element_query_duration(m_pipeline, GST_FORMAT_TIME, &ns);
    return ns > 0 ? (double)(ns / GST_MSECOND) : 0.0;
}

void PlayCore::close()
{
    teardown();
    emit("closed");
}

void PlayCore::teardown()
{
    GP_LOG("teardown enter");
    std::thread busThread;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        m_running = false;
        if (m_busThread.joinable()) busThread = std::move(m_busThread);
        if (m_pipeline) gst_element_set_state(m_pipeline, GST_STATE_NULL);
    }
    if (busThread.joinable()) busThread.join();  // 锁外 join, 避免与总线回调互等
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_bus) { gst_object_unref(m_bus); m_bus = nullptr; }
        if (m_pipeline) { gst_object_unref(m_pipeline); m_pipeline = nullptr; }
        m_sink = nullptr;
        m_audioConv = nullptr;
        for (int i = 0; i < 4; i++) m_audioTail[i] = nullptr;
        m_videoLinked = false;
        m_audioLinked = false;
        m_videoW = 0;
        m_videoH = 0;
        m_kickAtMs = 0;
        m_kickCount = 0;
        m_audioBaseMs = -1;
        m_videoBaseMs = -1;
        m_avOffsetApplied = false;
    }
    GP_LOG("teardown done");
}

bool PlayCore::buildPipeline(const std::string& uri)
{
    GstElement* pipeline = gst_pipeline_new("gstp");
    if (!pipeline) {
        GP_LOG("pipeline new failed");
        return false;
    }
    GstElement* src = NULL;
    GstElement* queue = gst_element_factory_make("queue", "demuxq");
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
                // B 站 CDN 防盗链: 浏览器 UA + Referer, 缺一 403 (真机实测)
                "user-agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
                NULL);
            GstStructure* hdrs = gst_structure_new("extra-headers",
                "Referer", G_TYPE_STRING, "https://www.bilibili.com",
                NULL);
            g_object_set(G_OBJECT(src), "extra-headers", hdrs, NULL);
            gst_structure_free(hdrs);
        }
    }
    if (!src || !queue || !demux) {
        GP_LOG("factory failed src=%d q=%d d=%d", !!src, !!queue, !!demux);
        gst_object_unref(pipeline);
        return false;
    }
    gst_bin_add_many(GST_BIN(pipeline), src, queue, demux, NULL);
    if (!gst_element_link_many(src, queue, demux, NULL)) {
        GP_LOG("src->demux link failed");
        gst_object_unref(pipeline);
        return false;
    }
    g_signal_connect(demux, "pad-added", G_CALLBACK(&PlayCore::onDemuxPadAdded), this);

    {
        std::lock_guard<std::mutex> lock(m_lock);
        m_pipeline = pipeline;
        m_videoLinked = false;
        m_audioLinked = false;
        m_sink = nullptr;
        m_audioConv = nullptr;
        m_videoW = 0;
        m_videoH = 0;
    }
    m_bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    m_running = true;
    m_busThread = std::thread(&PlayCore::busLoop, this);
    return true;
}

void PlayCore::onDemuxPadAdded(GstElement* demux, GstPad* pad, void* self)
{
    PlayCore* core = static_cast<PlayCore*>(self);
    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (!caps) caps = gst_pad_query_caps(pad, NULL);
    if (!caps) {
        GP_LOG("demux pad: no caps");
        return;
    }
    std::string name = gst_structure_get_name(gst_caps_get_structure(caps, 0));
    gst_caps_unref(caps);
    GP_LOG("demux pad: %s", name.c_str());

    if (name.rfind("video/", 0) == 0) {
        if (core->m_videoLinked) return;
        // 视频支路: h264parse -> mppvideodec (硬解) -> waylandsink.
        // 不加 videoflip/videoscale/capsfilter: 旋转交给 Weston 输出 transform,
        // 缩放交给 waylandsink render-rectangle + fill-mode=fit.
        GstElement* queueV = gst_element_factory_make("queue", "vq");
        GstElement* parse = gst_element_factory_make("h264parse", "vparse");
        GstElement* dec = gst_element_factory_make("mppvideodec", "vdec");
        GstElement* sink = gst_element_factory_make("waylandsink", "vsink");
        if (!queueV || !parse || !dec || !sink) {
            GP_LOG("video factory failed q=%d p=%d d=%d s=%d",
                   !!queueV, !!parse, !!dec, !!sink);
            return;
        }
        // 宿主矩形内等比适配 (自动信箱, 不裁切); rotate=identity: 全局空间
        // 横屏内容无需旋转.
        // sync 保持默认 true: 视频按音频时钟同步渲染 (0.7.5/0.7.6 实测
        // sync=false 视频超前音频, 音画不同步). 0.7.5 的 "卡住不自动播放"
        // 真因是音频支路从未预滚 (尾链元素状态未同步, ALSA state OPEN),
        // 由音频尾链 sync_state 修复 + ASYNC_DONE 后 checkKick 兜底.
        // layer 保持默认 (normal): 本固件 patched waylandsink 的 layer=bottom
        //   实现 SIGSEGV (真机 gst-launch 实测), 不可用.
        g_object_set(G_OBJECT(sink),
            "fill-mode", 1,            // fit: 保持宽高比
            "rotate-method", 0,        // identity
            "fullscreen", FALSE,
            NULL);
        gst_bin_add_many(GST_BIN(core->m_pipeline), queueV, parse, dec, sink, NULL);
        if (!gst_element_link_many(queueV, parse, dec, sink, NULL)) {
            GP_LOG("video link failed");
            gst_bin_remove_many(GST_BIN(core->m_pipeline), queueV, parse, dec, sink, NULL);
            return;
        }
        // caps 探针: 拿到视频分辨率 -> 计算拟合矩形 -> 运行时设置 render-rectangle
        GstPad* sinkPad = gst_element_get_static_pad(sink, "sink");
        gst_pad_add_probe(sinkPad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                          &PlayCore::capsProbe, core, NULL);
        gst_object_unref(sinkPad);
        core->m_sink = sink;
        GstPad* qPad = gst_element_get_static_pad(queueV, "sink");
        GstPadLinkReturn ret = gst_pad_link(pad, qPad);
        gst_object_unref(qPad);
        if (ret != GST_PAD_LINK_OK) {
            GP_LOG("video pad link failed ret=%d", (int)ret);
            gst_bin_remove_many(GST_BIN(core->m_pipeline), queueV, parse, dec, sink, NULL);
            core->m_sink = nullptr;
            return;
        }
        gst_element_sync_state_with_parent(queueV);
        gst_element_sync_state_with_parent(parse);
        gst_element_sync_state_with_parent(dec);
        gst_element_sync_state_with_parent(sink);
        core->m_videoLinked = true;
        GP_LOG("video branch linked");
        return;
    }

    if (name.rfind("audio/", 0) == 0) {
        if (core->m_audioLinked) return;
        // 音频支路: decodebin 承接 AAC(faad)/MP3(mpg123audiodec) 等任意编码.
        // 关键: decodebin 的 sink 是静态 pad, 必须显式 link; 解码输出经
        // pad-added 挂到 convert (旧版 link_many 断链失声的教训).
        GstElement* queueA = gst_element_factory_make("queue", "aq");
        GstElement* decode = gst_element_factory_make("decodebin", "adec");
        GstElement* convert = gst_element_factory_make("audioconvert", "aconv");
        GstElement* resample = gst_element_factory_make("audioresample", "ares");
        GstElement* volume = gst_element_factory_make("volume", "avol");
        GstElement* sink = gst_element_factory_make("alsasink", "asink");
        if (!queueA || !decode || !convert || !resample || !volume || !sink) {
            GP_LOG("audio factory failed q=%d d=%d c=%d r=%d v=%d s=%d",
                   !!queueA, !!decode, !!convert, !!resample, !!volume, !!sink);
            return;
        }
        gst_bin_add_many(GST_BIN(core->m_pipeline), queueA, decode, convert,
                         resample, volume, sink, NULL);
        // 尾链 (convert->...->sink) 静态链接; queueA->decodebin 单独链静态 sink.
        // 注意: 添加进运行中管线的元素不会自动跟进父状态, decodebin 挂接后
        // 必须逐个 sync_state (见 onAudioDecodePadAdded), 否则 alsasink 停在
        // NULL, 数据流堵死, 管道永远 PAUSED (位置恒 0, 不自动播放).
        if (!gst_element_link_many(convert, resample, volume, sink, NULL) ||
            !gst_element_link(queueA, decode)) {
            GP_LOG("audio link failed");
            gst_bin_remove_many(GST_BIN(core->m_pipeline), queueA, decode, convert,
                                resample, volume, sink, NULL);
            return;
        }
        g_signal_connect(decode, "pad-added",
                         G_CALLBACK(&PlayCore::onAudioDecodePadAdded), core);
        core->m_audioConv = convert;
        core->m_audioTail[0] = convert;
        core->m_audioTail[1] = resample;
        core->m_audioTail[2] = volume;
        core->m_audioTail[3] = sink;
        // SEGMENT 探针: 记录音频支路起点, 供音画起点对齐 (tryApplyAvOffset)
        GstPad* asinkPad = gst_element_get_static_pad(sink, "sink");
        gst_pad_add_probe(asinkPad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                          &PlayCore::audioSegProbe, core, NULL);
        gst_object_unref(asinkPad);
        GstPad* qPad = gst_element_get_static_pad(queueA, "sink");
        GstPadLinkReturn ret = gst_pad_link(pad, qPad);
        gst_object_unref(qPad);
        if (ret != GST_PAD_LINK_OK) {
            GP_LOG("audio pad link failed ret=%d", (int)ret);
            gst_bin_remove_many(GST_BIN(core->m_pipeline), queueA, decode, convert,
                                resample, volume, sink, NULL);
            core->m_audioConv = nullptr;
            return;
        }
        gst_element_sync_state_with_parent(queueA);
        gst_element_sync_state_with_parent(decode);
        core->m_audioLinked = true;
        GP_LOG("audio branch linked");
    }
}

void PlayCore::onAudioDecodePadAdded(GstElement* decodebin, GstPad* pad, void* self)
{
    PlayCore* core = static_cast<PlayCore*>(self);
    GstElement* convert = core->m_audioConv;
    if (!convert) return;
    GstPad* sinkPad = gst_element_get_static_pad(convert, "sink");
    if (sinkPad && !gst_pad_is_linked(sinkPad)) {
        if (gst_pad_link(pad, sinkPad) == GST_PAD_LINK_OK) {
            GP_LOG("audio decodebin linked");
            // 逐个同步尾链状态: convert/resample/volume/alsasink 加入运行中
            // 管线后不会自动跟进父状态, 不同步则数据流断在 convert.
            for (int i = 0; i < 4; i++) {
                if (core->m_audioTail[i]) {
                    gst_element_sync_state_with_parent(core->m_audioTail[i]);
                }
            }
        } else {
            GP_LOG("audio decodebin link failed");
        }
    }
    if (sinkPad) gst_object_unref(sinkPad);
}

// SEGMENT 事件起始 running time (ms); 异常返回 -1
static double segmentBaseMs(GstEvent* ev)
{
    const GstSegment* seg = NULL;
    gst_event_parse_segment(ev, &seg);
    if (!seg || seg->format != GST_FORMAT_TIME) return -1;
    double ms = (seg->base + seg->start) / GST_MSECOND;
    return ms;
}

GstPadProbeReturn PlayCore::audioSegProbe(GstPad* pad, GstPadProbeInfo* info, void* self)
{
    if (GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info)) != GST_EVENT_SEGMENT) {
        return GST_PAD_PROBE_OK;
    }
    PlayCore* core = static_cast<PlayCore*>(self);
    double ms = segmentBaseMs(GST_PAD_PROBE_INFO_EVENT(info));
    if (ms < 0) return GST_PAD_PROBE_OK;
    {
        std::lock_guard<std::mutex> lock(core->m_lock);
        if (core->m_audioBaseMs < 0) core->m_audioBaseMs = ms;
    }
    GP_LOG("audio segment base %.0f ms", ms);
    core->tryApplyAvOffset();
    return GST_PAD_PROBE_OK;
}

// 音画起点对齐: 视频支路起点比音频晚超过阈值时, 对视频 sink pad 施加
// 负的 pad offset, 消除恒定的 "画面落后音频" (起播画面卡 1s / 暂停画面晚 1s).
void PlayCore::tryApplyAvOffset()
{
    double deltaMs;
    GstPad* sinkPad = NULL;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_avOffsetApplied || m_audioBaseMs < 0 || m_videoBaseMs < 0 || !m_sink) return;
        deltaMs = m_videoBaseMs - m_audioBaseMs;
        m_avOffsetApplied = true;  // 只测一次, 之后的 SEGMENT (seek) 不再改
    }
    if (deltaMs > 150) {
        sinkPad = gst_element_get_static_pad(m_sink, "sink");
        if (sinkPad) {
            gst_pad_set_offset(sinkPad, -deltaMs * GST_MSECOND);
            GP_LOG("av offset: video base %.0f ms vs audio %.0f ms -> shift video -%.0f ms",
                   m_videoBaseMs, m_audioBaseMs, deltaMs);
            gst_object_unref(sinkPad);
        }
    } else {
        GP_LOG("av offset: video %.0f ms vs audio %.0f ms (delta %.0f, no shift)",
               m_videoBaseMs, m_audioBaseMs, deltaMs);
    }
}

GstPadProbeReturn PlayCore::capsProbe(GstPad* pad, GstPadProbeInfo* info, void* self)
{
    GstEventType et = GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info));
    if (et == GST_EVENT_SEGMENT) {
        // 视频支路起点 running time (音频侧见 audioSegProbe)
        PlayCore* core = static_cast<PlayCore*>(self);
        double ms = segmentBaseMs(GST_PAD_PROBE_INFO_EVENT(info));
        if (ms >= 0) {
            {
                std::lock_guard<std::mutex> lock(core->m_lock);
                if (core->m_videoBaseMs < 0) core->m_videoBaseMs = ms;
            }
            GP_LOG("video segment base %.0f ms", ms);
            core->tryApplyAvOffset();
        }
        return GST_PAD_PROBE_OK;
    }
    if (et != GST_EVENT_CAPS) {
        return GST_PAD_PROBE_OK;
    }
    GstCaps* caps = NULL;
    gst_event_parse_caps(GST_PAD_PROBE_INFO_EVENT(info), &caps);
    if (!caps || !gst_caps_is_fixed(caps)) return GST_PAD_PROBE_OK;
    GstStructure* st = gst_caps_get_structure(caps, 0);
    int w = 0, h = 0;
    if (!gst_structure_get_int(st, "width", &w) ||
        !gst_structure_get_int(st, "height", &h) || w <= 0 || h <= 0) {
        return GST_PAD_PROBE_OK;
    }
    PlayCore* core = static_cast<PlayCore*>(self);
    int rect[4];
    GstElement* sink = nullptr;
    {
        std::lock_guard<std::mutex> lock(core->m_lock);
        core->m_videoW = w;
        core->m_videoH = h;
        fitRect(w, h, core->m_rectIn, rect);
        sink = core->m_sink;  // 流线程写 / 探针线程读, 锁内快照
    }
    GP_LOG("caps %dx%d -> rect %d,%d,%d,%d", w, h, rect[0], rect[1], rect[2], rect[3]);
    if (sink) {
        // render-rectangle: GstValueArray of gint (gst-inspect 实锤)
        GValue arr = G_VALUE_INIT;
        GValue v = G_VALUE_INIT;
        g_value_init(&arr, GST_TYPE_ARRAY);
        g_value_init(&v, G_TYPE_INT);
        for (int i = 0; i < 4; i++) {
            g_value_set_int(&v, rect[i]);
            gst_value_array_append_value(&arr, &v);
        }
        g_value_unset(&v);
        g_object_set_property(G_OBJECT(sink), "render-rectangle", &arr);
        g_value_unset(&arr);
    }
    char line[64];
    snprintf(line, sizeof(line), "V %d %d", w, h);
    core->emit(line);
    return GST_PAD_PROBE_OK;
}

// 自动播放踢一脚: ASYNC_DONE 后管道可能仍卡在 PAUSED (音频支路竞态等),
// 到点检查, 未到 PLAYING 就 flush seek 到 0 (等效用户手动拖进度条, 真机
// 实测该操作能让数据流启动). 最多尝试 2 次, 每次 +2.5s.
// 注意: 总线线程存活期间 m_pipeline 不会被释放 (teardown 先 join 本线程),
// 锁外使用安全.
void PlayCore::checkKick()
{
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_kickAtMs == 0 || !m_pipeline) return;
        if (m_kickCount >= 2) return;
        if (nowMs() < m_kickAtMs) return;
        m_kickCount++;
        m_kickAtMs = nowMs() + 2500;
    }
    GstState cur = GST_STATE_VOID_PENDING, pend = GST_STATE_VOID_PENDING;
    gst_element_get_state(m_pipeline, &cur, &pend, 0);
    if (cur == GST_STATE_PLAYING) {
        std::lock_guard<std::mutex> lock(m_lock);
        m_kickAtMs = 0;
        GP_LOG("autoplay ok (PLAYING), kick cleared");
        return;
    }
    GP_LOG("autoplay kick: state=%d pending=%d -> flush seek 0", (int)cur, (int)pend);
    seekMs(0);
}

void PlayCore::busLoop()
{
    GP_LOG("bus thread started");
    while (true) {
        GstBus* bus;
        bool running;
        {
            std::lock_guard<std::mutex> lock(m_lock);
            bus = m_bus;
            running = m_running;
        }
        if (!bus || !running) break;
        GstMessage* msg = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND,
            (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_WARNING
                             | GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_STATE_CHANGED));
        if (!msg) {
            checkKick();
            continue;
        }
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
            std::string s = std::string("error: ") + (err ? err->message : "unknown");
            emit(s);
            if (err) g_error_free(err);
            if (dbg) g_free(dbg);
            break;
        }
        case GST_MESSAGE_STATE_CHANGED:
            // 真实播放状态: 仅认管道自身的状态变化 (子元素的不算).
            // PLAYING -> "play" / PAUSED -> "pause", 页面据此显示/隐藏控制条.
            if ((GstObject*)GST_MESSAGE_SRC(msg) == (GstObject*)m_pipeline) {
                GstState newState = GST_STATE_VOID_PENDING;
                gst_message_parse_state_changed(msg, NULL, &newState, NULL);
                if (newState == GST_STATE_PLAYING) {
                    GP_LOG("bus PLAYING");
                    emit("play");
                } else if (newState == GST_STATE_PAUSED) {
                    GP_LOG("bus PAUSED");
                    emit("pause");
                }
            }
            break;
        case GST_MESSAGE_ASYNC_DONE:
            GP_LOG("bus ASYNC_DONE");
            {
                std::lock_guard<std::mutex> lock(m_lock);
                m_kickAtMs = nowMs() + 1500;  // 1.5s 后开始自动播放检查
                m_kickCount = 0;
            }
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
    GP_LOG("bus thread exit");
}

}  // namespace gstplayer
