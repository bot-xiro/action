// JSGstPlayer.h
// GStreamer 视频播放器 —— MiniApp JSAPI 对象
// 提供 open/start/pause/close/seek API，仿 mplayer 接口
// 底层使用 GStreamer playbin + kmssink/waylandsink 输出

#pragma once

#include "jqutil_v2/jqutil.h"
#include <gst/gst.h>
#include <string>
#include <mutex>
#include <atomic>
#include <thread>

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

protected:
    void OnGCCollect() override;

private:
    void publishState(const std::string& state, const std::string& detail = "");
    void throwError(JQFunctionInfo& info, const std::string& msg);
    bool buildPipeline(const std::string& url, int pos_x, int pos_y, int pos_w, int pos_h, int aoenable);
    void destroyPipeline();
    static void onBusMessage(GstBus* bus, GstMessage* msg, gpointer user_data);
    void handleBusMessage(GstMessage* msg);

    std::mutex mutex_;
    GstElement* pipeline_;
    GstBus* bus_;
    int busWatchId_;
    std::atomic<bool> playing_;
    std::string currentUrl_;
    int posX_, posY_, posW_, posH_;

    // QuickJS callback on 'finish'
    JSValue finishCallback_;
    JSContext* ctx_;
};

}  // namespace gstplayer