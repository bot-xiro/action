#pragma once

#include "jqutil_v2/jqutil.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <gst/gst.h>

namespace bilistream {

class BiliStreamPlayer : public JQUTIL_NS::JQBaseObject {
public:
    BiliStreamPlayer();
    virtual ~BiliStreamPlayer();

    // JS 方法
    void open(JQUTIL_NS::JQFunctionInfo& info);
    void start(JQUTIL_NS::JQFunctionInfo& info);
    void pause(JQUTIL_NS::JQFunctionInfo& info);
    void resume(JQUTIL_NS::JQFunctionInfo& info);
    void seek(JQUTIL_NS::JQFunctionInfo& info);
    void close(JQUTIL_NS::JQFunctionInfo& info);
    void getDuration(JQUTIL_NS::JQFunctionInfo& info);
    void getPosition(JQUTIL_NS::JQFunctionInfo& info);

    // JS 信号：stateChanged.on(cb) / .off(cb)
    JQUTIL_NS::JQSignal<std::string> stateChanged;

private:
    bool buildPipeline(const std::string& uri, bool audio, const std::string& rect,
                       const std::string& fill, int canvasW, int canvasH);
    void applyCanvasContent(int srcW, int srcH);
    void teardown();
    void busLoop();
    void emitState(const std::string& state);
    void onQtdemuxPadAdded(GstPad* pad);
    void onDecodebinPadAdded(GstPad* pad);

    static void qtdemuxPadAddedCb(GstElement* element, GstPad* pad, gpointer userdata);
    static void decodebinPadAddedCb(GstElement* element, GstPad* pad, gpointer userdata);

    int appliedSrcW_ = 1280;
    int appliedSrcH_ = 720;

    GstElement* pipeline_ = nullptr;
    GstElement* demux_ = nullptr;
    GstElement* vparse_ = nullptr;
    GstElement* vdec_ = nullptr;
    GstElement* vqueue_ = nullptr;
    GstElement* vconvert_ = nullptr;
    GstElement* vscale_ = nullptr;
    GstElement* vcaps_ = nullptr;
    GstElement* vbox_ = nullptr;
    GstElement* vconvert2_ = nullptr;
    GstElement* decodebin_ = nullptr;
    GstElement* aconvert_ = nullptr;
    GstElement* aresample_ = nullptr;
    GstElement* acaps_early_ = nullptr;
    GstElement* aqueue_ = nullptr;
    GstElement* avolume_ = nullptr;
    GstElement* videoSink_ = nullptr;
    GstElement* audioSink_ = nullptr;
    GstPad* pendingAudioPad_ = nullptr;

    int canvasW_ = 0;
    int canvasH_ = 0;
    bool seeking_ = false;
    double seekTargetMs_ = 0.0;
    double durMs_ = 0.0;
    double posMs_ = 0.0;

    std::thread busThread_;
    std::atomic<bool> stopping_{false};
    std::mutex mutex_;
};

} // namespace bilistream
