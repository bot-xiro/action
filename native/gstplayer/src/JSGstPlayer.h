// JSGstPlayer.h
// GStreamer 视频播放器 —— MiniApp JSAPI 对象
// 提供 open/start/pause/close API，仿 mplayer 接口
// TODO: 真实 GStreamer pipeline 实现

#pragma once

#include "jqutil_v2/jqutil.h"
#include <string>
#include <mutex>
#include <atomic>

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

    std::mutex mutex_;
    void* pipeline_;  // GstElement* in real impl
    std::atomic<bool> playing_;
    int posX_, posY_, posW_, posH_;

    // QuickJS callback on 'finish'
    JSValue finishCallback_;
    JSContext* ctx_;
};

}  // namespace gstplayer