// JSGstPlayer.cpp
// GStreamer 视频播放器实现

#include "JSGstPlayer.h"
#include "jqutil_v2/jqutil.h"
#include <glib.h>
#include <cstring>

using namespace JQUTIL_NS;

namespace gstplayer {

JSGstPlayer::JSGstPlayer()
    : pipeline_(nullptr)
    , bus_(nullptr)
    , busWatchId_(0)
    , playing_(false)
    , posX_(0)
    , posY_(0)
    , posW_(960)
    , posH_(200)
    , ctx_(nullptr)
{
}

JSGstPlayer::~JSGstPlayer()
{
    closePipeline();
    if (ctx_ && !JS_IsUndefined(finishCallback_)) {
        JS_FreeValue(ctx_, finishCallback_);
    }
}

void JSGstPlayer::open(JQFunctionInfo& info)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (playing_) {
        throwError(info, "already playing");
        return;
    }

    JSContext* ctx = info.GetContext();
    JSValueConst options = info.Length() > 0 ? info[0] : JS_UNDEFINED;

    // 获取参数
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

    int pos_x = getIntProperty(ctx, options, "pos_x", 0);
    int pos_y = getIntProperty(ctx, options, "pos_y", 0);
    int pos_w = getIntProperty(ctx, options, "pos_w", 960);
    int pos_h = getIntProperty(ctx, options, "pos_h", 200);
    int aoenable = getIntProperty(ctx, options, "aoenable", 1);
    int loop = getIntProperty(ctx, options, "loop", 0);
    int decoder = getIntProperty(ctx, options, "decoder", 2); // 0硬解 1软解 2自动
    int fps = getIntProperty(ctx, options, "fps", 0);
    int volumn = getIntProperty(ctx, options, "volumn", 80);

    posX_ = pos_x;
    posY_ = pos_y;
    posW_ = pos_w;
    posH_ = pos_h;

    if (!buildPipeline(currentUrl_, pos_x, pos_y, pos_w, pos_h, aoenable)) {
        throwError(info, "build pipeline failed");
        return;
    }

    info.GetReturnValue().Set(0);
}

void JSGstPlayer::start(JQFunctionInfo& info)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pipeline_) {
        throwError(info, "pipeline not ready, call open first");
        return;
    }

    if (playing_) return;

    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        throwError(info, "set state PLAYING failed");
        return;
    }

    playing_ = true;
    info.GetReturnValue().Set(0);
}

void JSGstPlayer::pause(JQFunctionInfo& info)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pipeline_) {
        throwError(info, "pipeline not ready");
        return;
    }

    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        throwError(info, "set state PAUSED failed");
        return;
    }

    playing_ = false;
    info.GetReturnValue().Set(0);
}

void JSGstPlayer::close(JQFunctionInfo& info)
{
    std::lock_guard<std::mutex> lock(mutex_);
    closePipeline();
    info.GetReturnValue().Set(0);
}

void JSGstPlayer::closePipeline()
{
    if (!pipeline_) return;

    gst_element_set_state(pipeline_, GST_STATE_NULL);

    if (busWatchId_ > 0) {
        g_source_remove(busWatchId_);
        busWatchId_ = 0;
    }

    if (bus_) {
        gst_object_unref(bus_);
        bus_ = nullptr;
    }

    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
    bus_ = nullptr;
    busWatchId_ = 0;
    playing_ = false;
}

void JSGstPlayer::OnGCCollect()
{
    if (ctx_ && !JS_IsUndefined(finishCallback_)) {
        JS_FreeValue(ctx_, finishCallback_);
        finishCallback_ = JS_UNDEFINED;
    }
}

bool JSGstPlayer::buildPipeline(const std::string& url, int pos_x, int pos_y, int pos_w, int pos_h, int aoenable)
{
    closePipeline();

    // 简单的 playbin pipeline: playbin uri=... video-sink=kmssink
    // 支持网络 URL 和本地文件
    std::string pipelineDesc = "playbin uri=" + url;

    // 根据 aoenable 决定是否输出音频
    if (!aoenable) {
        pipelineDesc += " audio-sink=fakesink";
    }

    // video-sink: kmssink 需要设置 position/size
    // 注意: kmssink 需要 DRM/KMS 权限，且 hole 需要在同一位置
    // 这里先用 autovideosink，真机可改为 kmssink
    // 暂时使用 autovideosink 以便调试
    std::string videoSink = "autovideosink";

    // 如果需要 kmssink，可以设置:
    // videoSink = "kmssink plane-id=0 force-modesetting=false connector=DSI-1"

    pipelineDesc += " video-sink=" + videoSink;

    GError* err = nullptr;
    GstElement* p = gst_parse_launch(pipelineDesc.c_str(), &err);
    if (!p || err) {
        if (err) {
            LOGE("gst_parse_launch failed: %s", err->message);
            g_error_free(err);
        }
        return false;
    }

    pipeline_ = p;

    // Bus 消息监听
    bus_ = gst_element_get_bus(pipeline_);
    busWatchId_ = gst_bus_add_watch(bus_, &JSGstPlayer::onBusMessage, this);

    // 设置视频窗口位置/大小（如果使用 kmssink 且支持）
    // 暂不处理

    currentUrl_ = "";
    return true;
}

void JSGstPlayer::onBusMessage(GstBus* bus, GstMessage* msg, gpointer user_data)
{
    JSGstPlayer* self = static_cast<JSGstPlayer*>(user_data);
    self->handleBusMessage(msg);
    return G_SOURCE_CONTINUE;
}

void JSGstPlayer::handleBusMessage(GstMessage* msg)
{
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS: {
        std::lock_guard<std::mutex> lock(mutex_);
        playing_ = false;
        if (ctx_ && !JS_IsUndefined(finishCallback_)) {
            JSValueConst args[] = { JS_UNDEFINED };
            JSValue ret = JS_Call(ctx_, finishCallback_, JS_UNDEFINED, 0, args);
            if (JS_IsException(ret)) {
                LOGE("finish callback exception");
            }
            JS_FreeValue(ctx_, ret);
        }
        break;
    }
    case GST_MESSAGE_ERROR: {
        GError* err = nullptr;
        gchar* debug = nullptr;
        gst_message_parse_error(msg, &err, &debug);
        LOGE("GStreamer error: %s %s", err ? err->message : "unknown", debug ? debug : "");
        if (err) g_error_free(err);
        if (debug) g_free(debug);
        std::lock_guard<std::mutex> lock(mutex_);
        playing_ = false;
        break;
    }
    case GST_MESSAGE_STATE_CHANGED: {
        GstState old_state, new_state, pending_state;
        gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
        LOGI("state changed: %s -> %s", gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));
        if (new_state == GST_STATE_PLAYING) {
            std::lock_guard<std::mutex> lock(mutex_);
            playing_ = true;
        } else if (new_state == GST_STATE_PAUSED || new_state == GST_STATE_READY) {
            std::lock_guard<std::mutex> lock(mutex_);
            playing_ = false;
        }
        break;
    }
    default:
        break;
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