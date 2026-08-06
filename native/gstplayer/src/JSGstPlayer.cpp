// JSGstPlayer.cpp
// GStreamer 视频播放器真实实现
// pipeline: playbin(source=souphttpsrc+UA+Referer) → 自动 demux/decode → waylandsink(video) / alsasink(audio)
//
// 关键架构: miniapp 进程没有 GLib main loop（QuickJS 驱动），而 waylandsink 的 wayland
// 事件与 bus 消息都必须由 GLib main context 派发，否则 pipeline 永远卡在 preroll、
// 画面/声音都不出来（gst-launch 能播是因为它自带 main loop）。
// 因此本模块自建专用 GLib main loop 线程，所有 GStreamer 操作通过
// g_main_context_invoke 调度到该线程执行。
//
// 注意: 设备 GLib(2.7x) 的 g_main_context_invoke 在非 owner/非 thread-default 线程
// 调用时是异步 fire-and-forget（attach idle source 后立即返回，不等待执行），
// 因此 GstTask 带 done 标志，invokeGst 自旋等待任务完成，恢复同步契约，防止
// 栈上 GstTask 悬垂（旧版无等待导致 open() 未建链即返回 → start() 抛 not opened，
// 且悬垂 task 偶发崩溃在 g_object_set）。
//
// open 支持 options.sink = "kmssink" | "waylandsink"，便于真机调试切换
// （注意: kmssink 与设备 weston(drm-backend) 抢 DRM master 会卡死，默认 waylandsink）

#include "JSGstPlayer.h"
#include "jqutil_v2/jqutil.h"
#include <gst/gst.h>
#include <glib.h>
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

// ---------------- GLib main loop 专用线程 ----------------
// 所有 GStreamer 对象都在此线程创建/操作，bus watch 与 waylandsink 的
// wayland 事件源都挂到这个 context 上，由 g_main_loop_run 持续派发。
GMainContext* g_gstCtx = nullptr;
GMainLoop* g_gstLoop = nullptr;
GThread* g_gstThread = nullptr;

gpointer gst_loop_main(gpointer data)
{
    (void)data;
    GMainContext* ctx = g_main_context_new();
    g_main_context_push_thread_default(ctx);
    g_gstCtx = ctx;
    g_gstLoop = g_main_loop_new(ctx, FALSE);
    g_main_loop_run(g_gstLoop);
    g_main_loop_unref(g_gstLoop);
    g_main_context_pop_thread_default(ctx);
    g_main_context_unref(ctx);
    return nullptr;
}

void ensureGstLoop()
{
    if (g_gstThread) return;
    g_gstThread = g_thread_new("gst-main-loop", gst_loop_main, nullptr);
    // 等待 context 就绪（很快）
    while (!g_gstCtx) g_usleep(1000);
}

// ---------------- 同步调度到 GLib loop 线程 ----------------
// GstOp / GstTask / gst_task_cb / invokeGst 定义见 JSGstPlayer.h（类内成员，
// 静态成员函数 gst_task_cb 可访问私有方法；invokeGst 通过 g_main_context_invoke
// 把任务同步调度到 GLib loop 线程执行）

}  // namespace

int JSGstPlayer::gst_task_cb(void* data)
{
    GstTask* t = static_cast<GstTask*>(data);
    switch (t->op) {
    case GstOp::Build:
        try {
            t->self->buildPipeline(t->url);
        } catch (const std::exception& e) {
            t->error = e.what();
        }
        break;
    case GstOp::Start:
        t->ret = t->self->startInternal();
        break;
    case GstOp::Pause:
        t->ret = t->self->pauseInternal();
        break;
    case GstOp::Teardown:
        t->self->teardownPipeline();
        break;
    }
    t->done = true;
    return G_SOURCE_REMOVE;
}

void JSGstPlayer::invokeGst(GstTask* t)
{
    t->done = false;
    g_main_context_invoke(g_gstCtx, gst_task_cb, t);
    // 设备 GLib(2.7x) 的 g_main_context_invoke 在非 owner/非 thread-default 线程调用时是
    // 异步 fire-and-forget：attach idle source 后立即返回，不等待执行。此处自旋等待任务
    // 完成，恢复"同步调度"契约（等待期间调用线程栈帧保持有效，GstTask 不悬垂）。
    // 若 gst_task_cb 在当前线程直接执行（is_owner/acquire 路径），done 已为 true，立即返回。
    while (!t->done) {
        g_usleep(1000);
    }
}

JSGstPlayer::JSGstPlayer()
    : pipeline_(nullptr)
    , playing_(false)
    , posX_(0)
    , posY_(0)
    , posW_(960)
    , posH_(200)
    , audioEnable_(true)
    , useKmsSink_(false)   // 默认 waylandsink: kmssink 与 weston(drm-backend) 抢 DRM master 会卡死
    , finishCallback_(JS_UNDEFINED)
    , ctx_(nullptr)
{
    // 模块首次使用时初始化 GStreamer + 启动 GLib main loop 线程
    static bool gstInited = false;
    if (!gstInited) {
        gst_init(nullptr, nullptr);
        gstInited = true;
        LOGI("gstplayer: gst_init done (gstreamer %s)", gst_version_string());
    }
    ensureGstLoop();
}

JSGstPlayer::~JSGstPlayer()
{
    // 在 GLib loop 线程中清理 pipeline（析构可能发生在任意线程）
    if (g_gstCtx && pipeline_) {
        GstTask clean;
        clean.op = GstOp::Teardown;
        clean.self = this;
        invokeGst(&clean);
    }
    if (ctx_ && !JS_IsUndefined(finishCallback_)) {
        JS_FreeValue(ctx_, finishCallback_);
    }
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
                useKmsSink_ = (strcmp(s, "kmssink") == 0);
                JS_FreeCString(ctx, s);
            }
        } else {
            useKmsSink_ = false;  // 默认 waylandsink（kmssink 会与 weston 冲突卡死）
        }
        JS_FreeValue(ctx, val);
    }

    // 在 GLib loop 线程中构建 pipeline（bus watch / waylandsink 事件依赖该线程派发）
    GstTask task;
    task.op = GstOp::Build;
    task.self = this;
    task.url = url;
    invokeGst(&task);
    if (!task.error.empty()) {
        // 构建失败: 在 loop 线程清理残留对象
        GstTask clean;
        clean.op = GstOp::Teardown;
        clean.self = this;
        invokeGst(&clean);
        throwError(info, task.error);
        return;
    }

    LOGI("gstplayer open ok: url=%s pos=%d,%d,%d,%d audio=%d sink=%s",
         url.c_str(), posX_, posY_, posW_, posH_, audioEnable_ ? 1 : 0,
         useKmsSink_ ? "kmssink" : "waylandsink");
    info.GetReturnValue().Set(0);
}

void JSGstPlayer::start(JQFunctionInfo& info)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pipeline_) {
        throwError(info, "not opened");
        return;
    }
    GstTask task;
    task.op = GstOp::Start;
    task.self = this;
    invokeGst(&task);
    if (task.ret == GST_STATE_CHANGE_FAILURE) {
        playing_ = false;
        throwError(info, "gst set PLAYING failed");
        return;
    }
    playing_ = true;
    LOGI("gstplayer start -> PLAYING, ret=%d", task.ret);
    info.GetReturnValue().Set(0);
}

void JSGstPlayer::pause(JQFunctionInfo& info)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pipeline_) {
        throwError(info, "not opened");
        return;
    }
    GstTask task;
    task.op = GstOp::Pause;
    task.self = this;
    invokeGst(&task);
    playing_ = false;
    LOGI("gstplayer pause -> PAUSED, ret=%d", task.ret);
    info.GetReturnValue().Set(0);
}

void JSGstPlayer::close(JQFunctionInfo& info)
{
    std::lock_guard<std::mutex> lock(mutex_);
    GstTask task;
    task.op = GstOp::Teardown;
    task.self = this;
    invokeGst(&task);
    LOGI("gstplayer close");
    info.GetReturnValue().Set(0);
}

void JSGstPlayer::buildPipeline(const std::string& url)
{
    // 注意: 必须在 GLib loop 线程中调用（bus watch / waylandsink 事件源挂到该线程 context）
    if (!pipeline_) {
        // playbin: 自动 demux/decode/sink 选择，比手动 souphttpsrc+typefind+decodebin 链
        // 兼容性更好（IOT 点读笔系统部分 GStreamer 元素/参数不兼容，playbin 内部自适应）
        pipeline_ = gst_element_factory_make("playbin", "playbin");
        if (!pipeline_) throw std::runtime_error("playbin not available");
    }

    // flags 用 playbin 默认值（VIDEO|AUDIO|NATIVE_VIDEO|NATIVE_AUDIO，硬件解码优先）；
    // 不启用 DOWNLOAD（流式直放，避免 IOT 设备本地缓冲）

    // ---- 源: souphttpsrc + UA + Referer（playbin 的 source 属性接管，不设 uri）----
    // playbin 会对自定义 source 自动加 typefind + demux，UA/Referer 防 B 站 403
    GstElement* src = gst_element_factory_make("souphttpsrc", "src");
    if (!src) throw std::runtime_error("souphttpsrc not available");
    g_object_set(src, "location", url.c_str(), nullptr);
    g_object_set(src, "user-agent", kUserAgent, nullptr);
    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
        GstStructure* hdrs = gst_structure_new("headers", "Referer", G_TYPE_STRING, kReferer, nullptr);
        g_object_set(src, "extra-headers", hdrs, nullptr);
        gst_structure_free(hdrs);
    }
    g_object_set(pipeline_, "source", src, nullptr);
    gst_object_unref(src);  // playbin 已持有引用

    // ---- 视频 sink: waylandsink（默认）| kmssink（调试用）----
    const char* sinkName = useKmsSink_ ? "kmssink" : "waylandsink";
    GstElement* vsink = gst_element_factory_make(sinkName, "vsink");
    if (!vsink) throw std::runtime_error(std::string("video sink not available: ") + sinkName);
    if (useKmsSink_) {
        // kmssink 定位到 hole 区域: render-rectangle "<x, y, w, h>"
        gchar* rect = g_strdup_printf("<%d, %d, %d, %d>", posX_, posY_, posW_, posH_);
        g_object_set(vsink, "render-rectangle", rect, nullptr);
        g_free(rect);
    }
    g_object_set(pipeline_, "video-sink", vsink, nullptr);
    gst_object_unref(vsink);

    // ---- 音频 sink: alsasink device=speaker ----
    if (audioEnable_) {
        GstElement* asink = gst_element_factory_make("alsasink", "asink");
        if (!asink) throw std::runtime_error("alsasink not available");
        // 设备声卡: card0=rk817, card1=aw883xx(功放/扬声器)。
        // alsasink 默认选错设备（实测 "alsa is anolog mic" 链接失败）。
        // asound.conf 定义 pcm.speaker = plug→softvol→resample→hw:1,0，扬声器输出用这个别名。
        g_object_set(asink, "device", "speaker", nullptr);
        g_object_set(pipeline_, "audio-sink", asink, nullptr);
        gst_object_unref(asink);
    }

    // bus 监控（挂到当前线程即 GLib loop 线程的 default context）
    GstBus* bus = gst_element_get_bus(GST_ELEMENT(pipeline_));
    gst_bus_add_watch(bus, bus_watch, this);
    gst_object_unref(bus);

    gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_READY);
    // 待 start() 时切 PLAYING
}

void JSGstPlayer::teardownPipeline()
{
    // 注意: 本函数必须在 GLib loop 线程中调用（bus watch 持有 pipeline 引用）
    if (pipeline_) {
        gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }
    playing_ = false;
}

int JSGstPlayer::startInternal()
{
    // 必须在 GLib loop 线程中调用
    if (!pipeline_) return GST_STATE_CHANGE_FAILURE;
    return gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_PLAYING);
}

int JSGstPlayer::pauseInternal()
{
    // 必须在 GLib loop 线程中调用
    if (!pipeline_) return GST_STATE_CHANGE_FAILURE;
    return gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_PAUSED);
}

GstElement* JSGstPlayer::getPipeline() const
{
    return pipeline_;
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
