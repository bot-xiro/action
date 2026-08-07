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

// 遍历 playbin 子树找 souphttpsrc（playbin 的 child proxy 拿不到内部 source，
// 实测返回 NULL；元素在 READY 状态时已加入 bin，可直接遍历）
GstElement* findSoupSrc(GstElement* playbin)
{
    GstIterator* it = gst_bin_iterate_recurse(GST_BIN(playbin));
    GstElement* result = nullptr;
    GValue v = G_VALUE_INIT;
    while (gst_iterator_next(it, &v) == GST_ITERATOR_OK) {
        GObject* obj = g_value_get_object(&v);
        if (obj && GST_IS_ELEMENT(obj)) {
            const gchar* name = GST_OBJECT_NAME(obj);
            if (name && g_str_has_prefix(name, "souphttpsrc")) {
                result = GST_ELEMENT(gst_object_ref(obj));
                g_value_reset(&v);
                break;
            }
        }
        g_value_reset(&v);
    }
    g_value_unset(&v);
    gst_iterator_free(it);
    return result;
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

    std::ostringstream rect;
    if (posW > 0 && posH > 0) {
        // GstValueArray 字符串格式：<x, y, width, height>
        rect << "<" << posX << ", " << posY << ", " << posW << ", " << posH << ">";
    }

    // 关闭旧管线
    teardown();

    PLAYER_LOG("open uri=%s audio=%d rect=%s", uri.c_str(), audio ? 1 : 0, rect.str().c_str());
    bool ok = buildPipeline(uri, audio, rect.str());
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
    if (!playbin_) {
        info.GetReturnValue().ThrowInternalError("start: not opened");
        return;
    }
    gst_element_set_state(playbin_, GST_STATE_PLAYING);
    PLAYER_LOG("start set PLAYING");
    info.GetReturnValue().Set(true);
}

void GstPlayer::pause(JQFunctionInfo& info)
{
    if (!playbin_) {
        info.GetReturnValue().ThrowInternalError("pause: not opened");
        return;
    }
    gst_element_set_state(playbin_, GST_STATE_PAUSED);
    info.GetReturnValue().Set(true);
}

void GstPlayer::resume(JQFunctionInfo& info)
{
    if (!playbin_) {
        info.GetReturnValue().ThrowInternalError("resume: not opened");
        return;
    }
    gst_element_set_state(playbin_, GST_STATE_PLAYING);
    info.GetReturnValue().Set(true);
}

void GstPlayer::close(JQFunctionInfo& info)
{
    teardown();
    info.GetReturnValue().Set(true);
}

// ---- 管线构建（playbin 高层元素）----

bool GstPlayer::buildPipeline(const std::string& uri, bool audio, const std::string& rect)
{
    ensureGstInit();

    // playbin：URI 加载 → demux → 解码（mppvideodec 硬解自动优先）→ 音视频输出
    playbin_ = gst_element_factory_make("playbin", "gstplayer-playbin");
    if (!playbin_) {
        PLAYER_LOG("playbin factory failed");
        return false;
    }
    PLAYER_LOG("playbin created");

    g_object_set(playbin_, "uri", uri.c_str(), nullptr);
    PLAYER_LOG("uri set");

    // 进入 READY 让 playbin 创建内部 source（souphttpsrc），
    // 再设置浏览器 UA + Referer，绕过 B站 CDN 防盗链 403
    // （实测：B站 CDN 校验 Referer=bilibili.com，仅 UA 仍 403）
    gst_element_set_state(playbin_, GST_STATE_READY);
    GstElement* src = findSoupSrc(playbin_);
    if (src) {
        g_object_set(src, "user-agent",
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
            nullptr);
        gst_util_set_object_arg(G_OBJECT(src), "extra-headers", "referer=https://www.bilibili.com/");
        PLAYER_LOG("souphttpsrc UA+Referer set");
        gst_object_unref(src);
    } else {
        PLAYER_LOG("souphttpsrc not found, headers not set");
    }

    // 视频输出：waylandsink（weston DRM-backend；kmssink 会死锁，禁用）
    videoSink_ = gst_element_factory_make("waylandsink", "vsink");
    if (!videoSink_) {
        PLAYER_LOG("waylandsink factory failed");
        gst_object_unref(playbin_);
        playbin_ = nullptr;
        return false;
    }
    PLAYER_LOG("waylandsink created");
    // 可选：设置渲染区域（waylandsink 的 render-rectangle 属性，非所有版本支持）
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
    g_object_set(playbin_, "video-sink", videoSink_, nullptr);
    PLAYER_LOG("video-sink attached");

    // 音频输出：alsasink（device=speaker）；audio=false 时用 fakesink 静音
    audioSink_ = audio
        ? gst_element_factory_make("alsasink", "asink")
        : gst_element_factory_make("fakesink", "asink");
    if (audioSink_ && audio) {
        g_object_set(audioSink_, "device", "speaker", nullptr);
    }
    if (audioSink_) {
        g_object_set(playbin_, "audio-sink", audioSink_, nullptr);
    }
    PLAYER_LOG("audio-sink attached");

    // bus 轮询线程（不用 GLib 主循环，规避设备 GLib 差异）
    stopping_ = false;
    busThread_ = std::thread(&GstPlayer::busLoop, this);
    PLAYER_LOG("bus thread started");

    // open 到 PAUSED 预滚，由 start() 切换 PLAYING
    GstStateChangeReturn ret = gst_element_set_state(playbin_, GST_STATE_PAUSED);
    PLAYER_LOG("set_state PAUSED ret=%d", ret);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        teardown();
        return false;
    }

    return true;
}

void GstPlayer::teardown()
{
    PLAYER_LOG("teardown enter");
    stopping_ = true;
    if (busThread_.joinable()) {
        busThread_.join();
    }
    if (playbin_) {
        gst_element_set_state(playbin_, GST_STATE_NULL);
        gst_object_unref(playbin_);
    }
    playbin_ = nullptr;
    videoSink_ = nullptr;
    audioSink_ = nullptr;
    PLAYER_LOG("teardown done");
}

// ---- bus 消息循环（独立线程）----

void GstPlayer::busLoop()
{
    PLAYER_LOG("busLoop enter");
    GstBus* bus = gst_element_get_bus(playbin_);
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
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(playbin_)) {
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
