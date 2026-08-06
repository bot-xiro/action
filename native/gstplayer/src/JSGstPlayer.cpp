// JSGstPlayer.cpp
// GStreamer video player stub implementation
// TODO: Replace with real GStreamer pipeline once cross-compilation env is ready

#include "JSGstPlayer.h"
#include "jqutil_v2/jqutil.h"
#include <cstring>
#include <mutex>

using namespace JQUTIL_NS;

namespace gstplayer {

JSGstPlayer::JSGstPlayer()
    : pipeline_(nullptr)
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
    }

    LOGI("gstplayer open: url=%s pos=%d,%d,%d,%d", url.c_str(), posX_, posY_, posW_, posH_);
    info.GetReturnValue().Set(0);
}

void JSGstPlayer::start(JQFunctionInfo& info)
{
    std::lock_guard<std::mutex> lock(mutex_);
    playing_ = true;
    LOGI("gstplayer start");
    info.GetReturnValue().Set(0);
}

void JSGstPlayer::pause(JQFunctionInfo& info)
{
    std::lock_guard<std::mutex> lock(mutex_);
    playing_ = false;
    LOGI("gstplayer pause");
    info.GetReturnValue().Set(0);
}

void JSGstPlayer::close(JQFunctionInfo& info)
{
    std::lock_guard<std::mutex> lock(mutex_);
    pipeline_ = nullptr;
    playing_ = false;
    LOGI("gstplayer close");
    info.GetReturnValue().Set(0);
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