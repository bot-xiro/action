// JSGstPlayer.h
// GStreamer 视频播放器 —— MiniApp JSAPI 对象
// 真实 pipeline: souphttpsrc(+UA/Referer) → typefind → decodebin → kmssink(video) / alsasink(audio)
// 视频输出到 DRM overlay plane（hole-punch 与 mplayer 同思路）

#pragma once

#include "jqutil_v2/jqutil.h"
#include <string>
#include <mutex>
#include <atomic>

// 前向声明（避免头文件依赖 gst/gst.h，GStreamer 实现在 .cpp 中引入）
struct _GstPad;
typedef struct _GstPad GstPad;
struct _GstElement;
typedef struct _GstElement GstElement;

using namespace JQUTIL_NS;

namespace gstplayer {

class JSGstPlayer : public JQBaseObject {
public:
    JSGstPlayer();
    ~JSGstPlayer() override;

    void open(JQFunctionInfo& info);
    void start(JQFunctionInfo& info);
    void pause(JQFunctionInfo& info);
    void close(JQFunctionInfo& info);

    // decodebin 动态 pad 分发（供 pad-added 回调调用）
    void onDecodebinPad(GstPad* pad);
    GstElement* getPipeline() const;

    // GStreamer bus watch 回调需要（自由函数不能访问 private）
    void publishState(const std::string& state, const std::string& detail = "");

protected:
    void OnGCCollect() override;

private:
    void throwError(JQFunctionInfo& info, const std::string& msg);
    void buildPipeline(const std::string& url);
    void teardownPipeline();
    // 以下两个必须在 GLib main loop 线程中调用（由 GstTask 调度）
    GstStateChangeReturn startInternal();
    GstStateChangeReturn pauseInternal();

    std::mutex mutex_;
    GstElement* pipeline_;
    std::atomic<bool> playing_;
    int posX_, posY_, posW_, posH_;
    bool audioEnable_;
    bool useKmsSink_;         // true=kmssink, false=waylandsink
    GstElement* videoQueue_;
    GstElement* videoConvert_;
    GstElement* videoSink_;
    GstElement* audioQueue_;
    GstElement* audioConvert_;
    GstElement* audioSink_;

    // QuickJS callback on 'finish'
    JSValue finishCallback_;
    JSContext* ctx_;
};

}  // namespace gstplayer
