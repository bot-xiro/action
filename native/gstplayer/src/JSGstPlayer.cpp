// JSGstPlayer.cpp
// GStreamer 视频播放器真实实现
// pipeline: souphttpsrc(+UA+Referer) → typefind → decodebin → kmssink(video) / alsasink(audio)
// 视频用 render-rectangle 定位到 MiniApp hole 区域（与设备原生 mplayer 同思路）
// open 支持 options.sink = "kmssink" | "waylandsink"，便于真机调试切换

#include "JSGstPlayer.h"
#include "jqutil_v2/jqutil.h"
#include <gst/gst.h>
#include <cstring>
#include <mutex>
#include <string>

using namespace JQUTIL_NS;

namespace gstplayer {

namespace {

const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
const char* kReferer = "https://www.bilibili.com";

void on_pad_added(GstElement* element, GstPad* pad, gpointer user_data)
{
    (void)element;
    JSGstPlayer* self = static_cast<JSGstPlayer*>(user_data);
    self->onDecodebinPad(pad);
}

}  // namespace

JSGstPlayer::JSGstPlayer()
    : pipeline_(nullptr)
    , playing_(false)
    , posX_(0)
    , posY_(0)
    , posW_(960)
    , posH_(200)
    , audioEnable_(true)
    , useKmsSink_(true)
    , videoQueue_(nullptr)
    , videoConvert_(nullptr)
    , videoSink_(nullptr)
    , audioQueue_(nullptr)
    , audioConvert_(nullptr)
    , audioSink_(nullptr)
    , finishCallback_(JS_UNDEFINED)
    , ctx_(nullptr)
{
    // 模块首次使用时初始化 GStreamer
    static bool gstInited = false;
    if (!gstInited) {
        gst_init(nullptr, nullptr);
        gstInited = true;
        LOGI("gstplayer: gst_init done (gstreamer %s)", gst_version_string());
    }
}

JSGstPlayer::~JSGstPlayer()
{
    teardownPipeline();
    if (ctx_ && !JS_IsUndefined(finishCallback_)) {
        JS_FreeValue(ctx_, finishCallback_);
    }
}

void JSGstPlayer::onDecodebinPad(GstPad* pad)
{
    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (!caps) caps = gst_pad_query_caps(pad, nullptr);
    GstStructure* s = caps ? gst_caps_get_structure(caps, 0) : nullptr;
    if (!s) {
        if (caps) gst_caps_unref(caps);
        return;
    }
    const char* mime = gst_structure_get_name(s);
    bool isVideo = g_str_has_prefix(mime, "video/");
    bool isAudio = g_str_has_prefix(mime, "audio/");
    if (caps) gst_caps_unref(caps);

    GstElement* queue = nullptr;
    if (isVideo && videoQueue_) queue = videoQueue_;
    else if (isAudio && audioQueue_) queue = audioQueue_;
    if (!queue) return;

    GstPad* sinkpad = gst_element_get_static_pad(queue, "sink");
    if (!sinkpad || gst_pad_is_linked(sinkpad)) {
        if (sinkpad) gst_object_unref(sinkpad);
        return;
    }
    GstPadLinkReturn ret = gst_pad_link(pad, sinkpad);
    if (ret != GST_PAD_LINK_OK) {
        LOGE("gstplayer link %s to queue failed: %d", mime, ret);
    }
    gst_object_unref(sinkpad);
}

static gboolean bus_watch(GstBus* bus, GstMessage* msg, gpointer user_data)
{
    (void)bus;
    JSGstPlayer* self = static_cast<JSGstPlayer*>(user_data);
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
        GError* err = nullptr;
        gchar* dbg = nullptr;
        gst_message_parse_error(msg, &err, &dbg);
        self->publishState("error", err ? err->message : "unknown");
        LOGE("gstplayer pipeline error: %s (%s)", err ? err->message : "?", dbg ? dbg : "");
        if (err) g_error_free(err);
        if (dbg) g_free(dbg);
        break;
    }
    case GST_MESSAGE_EOS:
        self->publishState("finish", "eos");
        LOGI("gstplayer pipeline EOS");
        break;
    case GST_MESSAGE_STATE_CHANGED: {
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(self->getPipeline())) {
            GstState old, cur, pending;
            gst_message_parse_state_changed(msg, &old, &cur, &pending);
            LOGI("gstplayer state %s -> %s", gst_element_state_get_name(old), gst_element_state_get_name(cur));
        }
        break;
    }
    default:
        break;
    }
    return TRUE;
}

void JSGstPlayer::open(JQFunctionInfo& info)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (playing_ || pipeline_) {
        throwError(info, "already playing");
        return;
    }

    JSContext* ctx = info.GetContext();
    ctx_ = ctx;
    JSValueConst options = info.Length() > 0 ? info[0] : JS_UNDEFINED;

    // filename（URL 或本地路径）
    std::string url;
    {
        JSValue val = JS_GetPropertyStr(ctx, options, "filename");
        if (JS_IsString(val)) {
            const char* str = JS_ToCString(ctx, val);
            url = str ? str : "";
            JS_FreeCString(ctx, str);
        }
        JS_FreeValue(ctx, val);
    }
    if (url.empty()) {
        throwError(info, "filename empty");
        return;
    }

    // 位置与尺寸
    {
        JSValue val = JS_GetPropertyStr(ctx, options, "pos_x");
        if (JS_IsNumber(val)) JS_ToInt32(ctx, &posX_, val);
        JS_FreeValue(ctx, val);
        val = JS_GetPropertyStr(ctx, options, "pos_y");
        if (JS_IsNumber(val)) JS_ToInt32(ctx, &posY_, val);
        JS_FreeValue(ctx, val);
        val = JS_GetPropertyStr(ctx, options, "pos_w");
        if (JS_IsNumber(val)) JS_ToInt32(ctx, &posW_, val);
        JS_FreeValue(ctx, val);
        val = JS_GetPropertyStr(ctx, options, "pos_h");
        if (JS_IsNumber(val)) JS_ToInt32(ctx, &posH_, val);
        JS_FreeValue(ctx, val);
        val = JS_GetPropertyStr(ctx, options, "aoenable");
        if (JS_IsNumber(val)) {
            int aoe = 1;
            JS_ToInt32(ctx, &aoe, val);
            audioEnable_ = (aoe != 0);
        }
        JS_FreeValue(ctx, val);
        val = JS_GetPropertyStr(ctx, options, "sink");
        if (JS_IsString(val)) {
            const char* s = JS_ToCString(ctx, val);
            if (s) {
                useKmsSink_ = (strcmp(s, "waylandsink") != 0);
                JS_FreeCString(ctx, s);
            }
        }
        JS_FreeValue(ctx, val);
    }

    try {
        buildPipeline(url);
    } catch (const std::exception& e) {
        teardownPipeline();
        throwError(info, e.what());
        return;
    }

    LOGI("gstplayer open ok: url=%s pos=%d,%d,%d,%d audio=%d sink=%s",
         url.c_str(), posX_, posY_, posW_, posH_, audioEnable_ ? 1 : 0,
         useKmsSink_ ? "kmssink" : "waylandsink");
    info.GetReturnValue().Set(0);
}

void JSGstPlayer::buildPipeline(const std::string& url)
{
    if (!pipeline_) {
        pipeline_ = gst_pipeline_new("gstplayer-pipeline");
        if (!pipeline_) throw std::runtime_error("gst_pipeline_new failed");
    }

    // ---- 源: souphttpsrc + UA + Referer ----
    GstElement* src = gst_element_factory_make("souphttpsrc", "src");
    if (!src) throw std::runtime_error("souphttpsrc not available");
    g_object_set(src, "location", url.c_str(), nullptr);
    g_object_set(src, "user-agent", kUserAgent, nullptr);
    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
        GstStructure* hdrs = gst_structure_new("headers", "Referer", G_TYPE_STRING, kReferer, nullptr);
        g_object_set(src, "extra-headers", hdrs, nullptr);
        gst_structure_free(hdrs);
    }

    // ---- typefind + decodebin ----
    GstElement* typefind = gst_element_factory_make("typefind", "typefind");
    GstElement* decodebin = gst_element_factory_make("decodebin", "decodebin");
    if (!typefind || !decodebin) throw std::runtime_error("typefind/decodebin not available");

    // ---- 视频分支: queue ! videoconvert ! (kmssink|waylandsink) ----
    videoQueue_ = gst_element_factory_make("queue", "vqueue");
    videoConvert_ = gst_element_factory_make("videoconvert", "vconvert");
    const char* sinkName = useKmsSink_ ? "kmssink" : "waylandsink";
    videoSink_ = gst_element_factory_make(sinkName, "vsink");
    if (!videoQueue_ || !videoConvert_ || !videoSink_) {
        throw std::runtime_error(std::string("video branch elements not available: ") + sinkName);
    }
    if (useKmsSink_) {
        // kmssink 定位到 hole 区域: render-rectangle "<x, y, w, h>"
        gchar* rect = g_strdup_printf("<%d, %d, %d, %d>", posX_, posY_, posW_, posH_);
        g_object_set(videoSink_, "render-rectangle", rect, nullptr);
        g_free(rect);
    }

    // ---- 音频分支: queue ! audioconvert ! alsasink ----
    if (audioEnable_) {
        audioQueue_ = gst_element_factory_make("queue", "aqueue");
        audioConvert_ = gst_element_factory_make("audioconvert", "aconvert");
        audioSink_ = gst_element_factory_make("alsasink", "asink");
        if (!audioQueue_ || !audioConvert_ || !audioSink_) {
            throw std::runtime_error("audio branch elements not available");
        }
    }

    // ---- 组装 ----
    gst_bin_add_many(GST_BIN(pipeline_), src, typefind, decodebin,
                     videoQueue_, videoConvert_, videoSink_, nullptr);
    if (audioEnable_) {
        gst_bin_add_many(GST_BIN(pipeline_), audioQueue_, audioConvert_, audioSink_, nullptr);
    }

    if (!gst_element_link_many(src, typefind, decodebin, nullptr)) {
        throw std::runtime_error("link src->typefind->decodebin failed");
    }
    if (!gst_element_link_many(videoQueue_, videoConvert_, videoSink_, nullptr)) {
        throw std::runtime_error("link video branch failed");
    }
    if (audioEnable_ &&
        !gst_element_link_many(audioQueue_, audioConvert_, audioSink_, nullptr)) {
        throw std::runtime_error("link audio branch failed");
    }

    // decodebin 动态 pad -> 视频/音频分支
    g_signal_connect(decodebin, "pad-added", G_CALLBACK(on_pad_added), this);

    // bus 监控
    GstBus* bus = gst_element_get_bus(GST_ELEMENT(pipeline_));
    gst_bus_add_watch(bus, bus_watch, this);
    gst_object_unref(bus);

    gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_READY);
    // 待 start() 时切 PLAYING
}

void JSGstPlayer::start(JQFunctionInfo& info)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pipeline_) {
        throwError(info, "not opened");
        return;
    }
    playing_ = true;
    GstStateChangeReturn ret = gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_PLAYING);
    LOGI("gstplayer start -> PLAYING, ret=%d", ret);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        playing_ = false;
        throwError(info, "gst set PLAYING failed");
        return;
    }
    info.GetReturnValue().Set(0);
}

void JSGstPlayer::pause(JQFunctionInfo& info)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pipeline_) {
        throwError(info, "not opened");
        return;
    }
    playing_ = false;
    GstStateChangeReturn ret = gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_PAUSED);
    LOGI("gstplayer pause -> PAUSED, ret=%d", ret);
    info.GetReturnValue().Set(0);
}

void JSGstPlayer::close(JQFunctionInfo& info)
{
    std::lock_guard<std::mutex> lock(mutex_);
    teardownPipeline();
    LOGI("gstplayer close");
    info.GetReturnValue().Set(0);
}

void JSGstPlayer::teardownPipeline()
{
    if (pipeline_) {
        gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }
    videoQueue_ = nullptr;
    videoConvert_ = nullptr;
    videoSink_ = nullptr;
    audioQueue_ = nullptr;
    audioConvert_ = nullptr;
    audioSink_ = nullptr;
    playing_ = false;
}

GstElement* JSGstPlayer::getPipeline() const
{
    return static_cast<GstElement*>(pipeline_);
}

void JSGstPlayer::OnGCCollect()
{
    if (ctx_ && !JS_IsUndefined(finishCallback_)) {
        JS_FreeValue(ctx_, finishCallback_);
        finishCallback_ = JS_UNDEFINED;
    }
}

void JSGstPlayer::publishState(const std::string& state, const std::string& detail)
{
    LOGI("gstplayer state %s %s", state.c_str(), detail.c_str());
}

void JSGstPlayer::throwError(JQFunctionInfo& info, const std::string& msg)
{
    LOGE("gstplayer error %s", msg.c_str());
    info.GetReturnValue().ThrowInternalError("%s", msg.c_str());
}

}  // namespace gstplayer
