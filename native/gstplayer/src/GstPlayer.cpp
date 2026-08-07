#include "GstPlayer.h"

#include <sstream>
#include <syslog.h>

using namespace JQUTIL_NS;

namespace gstplayer {

#define PLAYER_LOG(fmt, ...) syslog(LOG_ERR, "[gstplayer] " fmt, ##__VA_ARGS__)

namespace {

// 一次性 GStreamer 初始化
void ensureGstInit()
{
    static bool inited = false;
    if (!inited) {
        PLAYER_LOG("gst_init enter");
        gst_init(nullptr, nullptr);
        PLAYER_LOG("gst_init done");
        inited = true;
    }
}

// ---- JS 参数解析辅助 ----
std::string jsGetString(JSContext* ctx, JSValueConst obj, const char* key)
{
    JSValue v = JS_GetPropertyStr(ctx, obj, key);
    std::string result;
    if (JS_IsString(v)) {
        const char* s = JS_ToCString(ctx, v);
        if (s) result = s;
        JS_FreeCString(ctx, s);
    }
    JS_FreeValue(ctx, v);
    return result;
}

bool jsGetBool(JSContext* ctx, JSValueConst obj, const char* key, bool def)
{
    JSValue v = JS_GetPropertyStr(ctx, obj, key);
    bool result = def;
    if (JS_IsBool(v)) result = JS_ToBool(ctx, v);
    JS_FreeValue(ctx, v);
    return result;
}

int jsGetInt(JSContext* ctx, JSValueConst obj, const char* key, int def)
{
    JSValue v = JS_GetPropertyStr(ctx, obj, key);
    int result = def;
    if (JS_IsNumber(v)) {
        int32_t i = 0;
        if (JS_ToInt32(ctx, &i, v) == 0) result = i;
    }
    JS_FreeValue(ctx, v);
    return result;
}

}  // namespace

GstPlayer::GstPlayer() = default;

GstPlayer::~GstPlayer()
{
    teardown();
}

// ---- JS 方法 ----

void GstPlayer::open(JQFunctionInfo& info)
{
    JSContext* ctx = info.GetContext();
    if (info.Length() < 1 || !JS_IsObject(info[0])) {
        info.GetReturnValue().ThrowTypeError("open: expected options object");
        return;
    }

    JSValueConst opt = info[0];
    std::string uri = jsGetString(ctx, opt, "uri");
    if (uri.empty()) {
        // 兼容历史字段 filename
        uri = jsGetString(ctx, opt, "filename");
    }
    if (uri.empty()) {
        info.GetReturnValue().ThrowTypeError("open: uri required");
        return;
    }

    bool audio = jsGetBool(ctx, opt, "audio", true);
    int posX = jsGetInt(ctx, opt, "pos_x", 0);
    int posY = jsGetInt(ctx, opt, "pos_y", 0);
    int posW = jsGetInt(ctx, opt, "pos_w", 0);
    int posH = jsGetInt(ctx, opt, "pos_h", 0);
    std::string fill = jsGetString(ctx, opt, "fill");  // "fit"/"crop"/"stretch"，空=fit

    std::ostringstream rect;
    if (posW > 0 && posH > 0) {
        // GstValueArray 字符串格式：<x, y, width, height>
        rect << "<" << posX << ", " << posY << ", " << posW << ", " << posH << ">";
    }

    // 关闭旧管线
    teardown();

    PLAYER_LOG("open uri=%s audio=%d rect=%s fill=%s", uri.c_str(), audio ? 1 : 0, rect.str().c_str(), fill.c_str());
    bool ok = buildPipeline(uri, audio, rect.str(), fill);
    PLAYER_LOG("open buildPipeline ret=%d", ok ? 1 : 0);
    if (!ok) {
        teardown();
        info.GetReturnValue().ThrowInternalError("open: pipeline build failed");
        return;
    }

    info.GetReturnValue().Set(true);
}

void GstPlayer::start(JQFunctionInfo& info)
{
    PLAYER_LOG("start enter");
    if (!pipeline_) {
        info.GetReturnValue().ThrowInternalError("start: not opened");
        return;
    }
    gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    PLAYER_LOG("start set PLAYING");
    info.GetReturnValue().Set(true);
}

void GstPlayer::pause(JQFunctionInfo& info)
{
    if (!pipeline_) {
        info.GetReturnValue().ThrowInternalError("pause: not opened");
        return;
    }
    gst_element_set_state(pipeline_, GST_STATE_PAUSED);
    info.GetReturnValue().Set(true);
}

void GstPlayer::resume(JQFunctionInfo& info)
{
    if (!pipeline_) {
        info.GetReturnValue().ThrowInternalError("resume: not opened");
        return;
    }
    gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    info.GetReturnValue().Set(true);
}

void GstPlayer::close(JQFunctionInfo& info)
{
    teardown();
    info.GetReturnValue().Set(true);
}

// ---- 管线构建（手动管线：souphttpsrc → queue → decodebin → 音视频分流）----

bool GstPlayer::buildPipeline(const std::string& uri, bool audio, const std::string& rect, const std::string& fill)
{
    ensureGstInit();

    // 不用 playbin：其内部 souphttpsrc 无法获取（bin 遍历不可见、child proxy 返回 NULL），
    // 而 B站 CDN 必须设置浏览器 UA + Referer 才能过防盗链。
    // 手动构建：souphttpsrc（直接创建并设头）→ queue → decodebin（pad-added 按媒体类型分流）
    pipeline_ = gst_pipeline_new("gstplayer-pipeline");
    if (!pipeline_) {
        PLAYER_LOG("pipeline factory failed");
        return false;
    }

    GstElement* src = gst_element_factory_make("souphttpsrc", "src");
    GstElement* queue = gst_element_factory_make("queue", "qsrc");
    decodebin_ = gst_element_factory_make("decodebin", "decode");
    if (!src || !queue || !decodebin_) {
        PLAYER_LOG("factory failed src=%d queue=%d decodebin=%d",
            src ? 1 : 0, queue ? 1 : 0, decodebin_ ? 1 : 0);
        if (src) gst_object_unref(src);
        if (queue) gst_object_unref(queue);
        if (decodebin_) gst_object_unref(decodebin_);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        decodebin_ = nullptr;
        return false;
    }

    g_object_set(src, "location", uri.c_str(), nullptr);
    g_object_set(src, "user-agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        nullptr);
    // 直接构造 GstStructure 设置 extra-headers（gst_util_set_object_arg 字符串解析
    // 在设备上不可靠，可能解析失败导致 Referer 未生效）
    GstStructure* hdrs = gst_structure_new_empty("headers");
    gst_structure_set(hdrs, "referer", G_TYPE_STRING, "https://www.bilibili.com/", nullptr);
    g_object_set(src, "extra-headers", hdrs, nullptr);
    gst_structure_free(hdrs);
    // 回读验证属性是否真正设置成功
    GstStructure* back = nullptr;
    g_object_get(src, "extra-headers", &back, nullptr);
    if (back) {
        gchar* hs = gst_structure_to_string(back);
        PLAYER_LOG("extra-headers back: %s", hs ? hs : "(null)");
        if (hs) g_free(hs);
        gst_structure_free(back);
    } else {
        PLAYER_LOG("extra-headers back: (null) - SET FAILED");
    }
    PLAYER_LOG("souphttpsrc created, UA+Referer set");

    // 视频输出：waylandsink（weston DRM-backend；kmssink 会死锁，禁用）
    videoSink_ = gst_element_factory_make("waylandsink", "vsink");
    if (!videoSink_) {
        PLAYER_LOG("waylandsink factory failed");
        teardown();
        return false;
    }
    PLAYER_LOG("waylandsink created");
    // 填充模式：fit=等比留黑边（默认）、crop=等比裁剪填满、stretch=拉伸变形
    if (!fill.empty() && (fill == "crop" || fill == "stretch")) {
        GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(videoSink_), "fill-mode");
        if (pspec) {
            g_object_set(videoSink_, "fill-mode", fill == "crop" ? 2 : 0, nullptr);
            PLAYER_LOG("fill-mode set: %s", fill.c_str());
        }
    }
    if (!rect.empty()) {
        GParamSpec* pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(videoSink_), "render-rectangle");
        if (pspec) {
            // render-rectangle 是 GstValueArray of gint（Write only），
            // g_object_set 传 C 字符串会触发 GLib 类型不匹配 critical/abort 崩溃；
            // gst_util_set_object_arg 会把 "<x, y, w, h>" 字符串解析为值数组
            gst_util_set_object_arg(G_OBJECT(videoSink_), "render-rectangle", rect.c_str());
            PLAYER_LOG("render-rectangle set: %s", rect.c_str());
        }
    }

    // 音频输出：alsasink（device=speaker）；audio=false 时用 fakesink 静音
    audioSink_ = audio
        ? gst_element_factory_make("alsasink", "asink")
        : gst_element_factory_make("fakesink", "asink");
    if (audioSink_ && audio) {
        g_object_set(audioSink_, "device", "speaker", nullptr);
    }
    PLAYER_LOG("audio-sink created: %s", audio ? "alsasink" : "fakesink");

    // 组装：src → queue → decodebin（动态 pad 分流到 videoSink_/audioSink_）
    gst_bin_add_many(GST_BIN(pipeline_), src, queue, decodebin_, videoSink_, audioSink_, nullptr);
    if (!gst_element_link_many(src, queue, decodebin_, nullptr)) {
        PLAYER_LOG("link src->decodebin failed");
        teardown();
        return false;
    }
    g_signal_connect(decodebin_, "pad-added", G_CALLBACK(GstPlayer::decodebinPadAddedCb), this);
    PLAYER_LOG("pipeline linked");

    // bus 轮询线程（不用 GLib 主循环，规避设备 GLib 差异）
    stopping_ = false;
    busThread_ = std::thread(&GstPlayer::busLoop, this);
    PLAYER_LOG("bus thread started");

    // open 到 PAUSED 预滚，由 start() 切换 PLAYING
    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PAUSED);
    PLAYER_LOG("set_state PAUSED ret=%d", ret);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        teardown();
        return false;
    }

    return true;
}

// decodebin pad-added 静态回调 → 转成员函数（userdata=this）
void GstPlayer::decodebinPadAddedCb(GstElement* element, GstPad* pad, gpointer userdata)
{
    GstPlayer* self = static_cast<GstPlayer*>(userdata);
    self->onDecodebinPadAdded(pad);
}

void GstPlayer::onDecodebinPadAdded(GstPad* pad)
{
    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (!caps) {
        PLAYER_LOG("pad-added: no caps");
        return;
    }
    const GstStructure* s = gst_caps_get_structure(caps, 0);
    const gchar* media = s ? gst_structure_get_name(s) : nullptr;  // "video/x-h264" / "audio/mpeg"
    PLAYER_LOG("pad-added: %s", media ? media : "?");
    GstElement* sink = nullptr;
    if (media && g_str_has_prefix(media, "video/")) {
        sink = videoSink_;
    } else if (media && g_str_has_prefix(media, "audio/")) {
        sink = audioSink_;
    }
    if (sink) {
        GstPad* sinkPad = gst_element_get_static_pad(sink, "sink");
        if (sinkPad) {
            GstPadLinkReturn r = gst_pad_link(pad, sinkPad);
            PLAYER_LOG("pad link ret=%d", r);
            gst_object_unref(sinkPad);
        } else {
            PLAYER_LOG("sink pad not found for %s", media ? media : "?");
        }
    } else {
        PLAYER_LOG("no sink for %s", media ? media : "?");
    }
    gst_caps_unref(caps);
}

void GstPlayer::teardown()
{
    PLAYER_LOG("teardown enter");
    stopping_ = true;
    if (busThread_.joinable()) {
        busThread_.join();
    }
    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
    }
    pipeline_ = nullptr;
    decodebin_ = nullptr;
    videoSink_ = nullptr;
    audioSink_ = nullptr;
    PLAYER_LOG("teardown done");
}

// ---- bus 消息循环（独立线程）----

void GstPlayer::busLoop()
{
    PLAYER_LOG("busLoop enter");
    GstBus* bus = gst_element_get_bus(pipeline_);
    if (!bus) {
        PLAYER_LOG("busLoop get_bus failed");
        return;
    }

    while (!stopping_) {
        GstMessage* msg = gst_bus_timed_pop_filtered(
            bus, 100 * GST_MSECOND,
            static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR | GST_MESSAGE_STATE_CHANGED));
        if (!msg) continue;

        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            PLAYER_LOG("bus EOS");
            emitState("ended");
            break;
        case GST_MESSAGE_ERROR: {
            gchar* debug = nullptr;
            GError* err = nullptr;
            gst_message_parse_error(msg, &err, &debug);
            std::string emsg = err && err->message ? err->message : "unknown";
            PLAYER_LOG("bus ERROR: %s", emsg.c_str());
            if (debug) g_free(debug);
            if (err) g_error_free(err);
            emitState("error:" + emsg);
            break;
        }
        case GST_MESSAGE_STATE_CHANGED: {
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline_)) {
                GstState oldState, newState;
                gst_message_parse_state_changed(msg, &oldState, &newState, nullptr);
                PLAYER_LOG("bus state %s -> %s",
                    gst_element_state_get_name(oldState), gst_element_state_get_name(newState));
                if (newState == GST_STATE_PLAYING) {
                    emitState("playing");
                } else if (newState == GST_STATE_PAUSED) {
                    emitState("paused");
                }
            }
            break;
        }
        default:
            break;
        }
        gst_message_unref(msg);
    }

    gst_object_unref(bus);
    PLAYER_LOG("busLoop exit");
}

void GstPlayer::emitState(const std::string& state)
{
    // JQSignal 线程安全：非本线程 emit 自动 post 到 JS 线程
    stateChanged.emit(state);
}

// ---- 模块导出 ----

static JSValue createGstPlayer(JQModuleEnv* env)
{
    JQFunctionTemplateRef tpl = JQFunctionTemplate::New(env, "gstPlayer");
    tpl->InstanceTemplate()->setObjectCreator([]() {
        static GstPlayer* player = []() {
            GstPlayer* instance = new GstPlayer();
            instance->REF();
            return instance;
        }();
        return player;
    });

    tpl->SetProtoMethod("open", &GstPlayer::open);
    tpl->SetProtoMethod("start", &GstPlayer::start);
    tpl->SetProtoMethod("pause", &GstPlayer::pause);
    tpl->SetProtoMethod("resume", &GstPlayer::resume);
    tpl->SetProtoMethod("close", &GstPlayer::close);

    // JS 侧: gstPlayer.stateChanged.on(cb) / .off(cb)
    tpl->InstanceTemplate()->Set("stateChanged", &GstPlayer::stateChanged);

    return tpl->CallConstructor();
}

void gstplayer_init(JQModuleEnv* env)
{
    env->setModuleExport("gstPlayer", createGstPlayer(env));
}

}  // namespace gstplayer
