// JS 包壳：JS 端可见 API、协议、线程模型。
#pragma once
#include "PlayerModule.hpp"
#include <jqutil_v2/jqutil.h>
#include <memory>
#include <mutex>

using namespace JQUTIL_NS;

class JSPlayerModule : public JQPublishObject
{
public:
    JSPlayerModule();
    ~JSPlayerModule();

    // 同步：版本
    void getVersion(JQFunctionInfo &info);
    // 同步：状态快照 {state, positionMs, durationMs, lastError, title}
    void getStatus(JQFunctionInfo &info);
    // 同步：参数校验 + 是否直播
    void validate(JQFunctionInfo &info);

    // 异步 Promise：加载 {url,type}
    void loadP(JQAsyncInfo &info);
    // 异步 Promise：播放
    void playP(JQAsyncInfo &info);
    // 异步 Promise：暂停
    void pauseP(JQAsyncInfo &info);
    // 异步 Promise：恢复
    void resumeP(JQAsyncInfo &info);
    // 异步 Promise：停止
    void stopP(JQAsyncInfo &info);
    // 异步 Promise：快进/快退 {seconds}
    void seekP(JQAsyncInfo &info);
    // 异步 Promise：刷新位置/时长
    void refreshP(JQAsyncInfo &info);

private:
    std::unique_ptr<PlayerPipeline> obj_;
    mutable std::mutex objMutex_;

    PlayerPipeline *getObj() const {
        std::lock_guard<std::mutex> lock(objMutex_);
        return obj_.get();
    }
};

extern JSValue createPlayerModule(JQModuleEnv *env);