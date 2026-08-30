// JS 包壳实现：完整播放器控制 API（进度条/暂停/快进所需）。
#include "JSPlayerModule.hpp"
#include <Exceptions/Exception.hpp>

// PlayerStatus -> Bson，跨层稳定协议
static Bson statusToBson(const PlayerStatus &s)
{
    return Bson::object{
        {"state", static_cast<int>(s.state)},
        {"positionMs", static_cast<double>(s.positionMs)},
        {"durationMs", static_cast<double>(s.durationMs)},
        {"lastError", s.lastError},
        {"title", s.title},
    };
}

extern JSValue createPlayerModule(JQModuleEnv *env)
{
    JQFunctionTemplateRef tpl = JQFunctionTemplate::New(env, "PlayerModule");
    tpl->InstanceTemplate()->setObjectCreator([]() { return new JSPlayerModule(); });

    tpl->SetProtoMethod("getVersion", &JSPlayerModule::getVersion); // sync
    tpl->SetProtoMethod("getStatus", &JSPlayerModule::getStatus);   // sync
    tpl->SetProtoMethod("validate", &JSPlayerModule::validate);     // sync

    tpl->SetProtoMethodPromise("loadP", &JSPlayerModule::loadP);
    tpl->SetProtoMethodPromise("playP", &JSPlayerModule::playP);
    tpl->SetProtoMethodPromise("pauseP", &JSPlayerModule::pauseP);
    tpl->SetProtoMethodPromise("resumeP", &JSPlayerModule::resumeP);
    tpl->SetProtoMethodPromise("stopP", &JSPlayerModule::stopP);
    tpl->SetProtoMethodPromise("seekP", &JSPlayerModule::seekP);
    tpl->SetProtoMethodPromise("refreshP", &JSPlayerModule::refreshP);

    JSPlayerModule::InitTpl(tpl);
    return tpl->CallConstructor();
}

JSPlayerModule::JSPlayerModule()
    : obj_(std::make_unique<PlayerPipeline>()) {}

JSPlayerModule::~JSPlayerModule() = default;

void JSPlayerModule::getVersion(JQFunctionInfo &info)
{
    try {
        ASSERT(info.Length() == 0);
        info.GetReturnValue().Set("1.0.0-pvplayer");
    } catch (const std::exception &e) {
        info.GetReturnValue().ThrowInternalError(e.what());
    }
}

void JSPlayerModule::getStatus(JQFunctionInfo &info)
{
    try {
        ASSERT(info.Length() == 0);
        PlayerPipeline *o = getObj(); ASSERT(o != nullptr);
        info.GetReturnValue().Set(statusToBson(o->getStatus()));
    } catch (const std::exception &e) {
        info.GetReturnValue().ThrowInternalError(e.what());
    }
}

void JSPlayerModule::validate(JQFunctionInfo &info)
{
    try {
        ASSERT(info.Length() == 1);
        Bson arg = JSValueToBson(info.GetContext(), info[0]);
        std::string url = arg["url"].string_value();
        std::string type = arg["type"].string_value();

        std::string reason;
        bool ok = true;
        if (!PlayerPipeline::validateUrl(url)) { ok = false; reason = "invalid url"; }
        else if (!PlayerPipeline::validateType(type)) { ok = false; reason = "invalid type"; }

        info.GetReturnValue().Set(Bson::object{
            {"ok", ok},
            {"reason", reason},
            {"isLive", PlayerPipeline::isLive(type)},
        });
    } catch (const std::exception &e) {
        info.GetReturnValue().ThrowInternalError(e.what());
    }
}

void JSPlayerModule::loadP(JQAsyncInfo &info)
{
    try {
        ASSERT(info.Length() == 1);
        ASSERT(info[0].is_object());
        std::string url = info[0]["url"].string_value();
        std::string type = info[0]["type"].string_value();

        PlayerPipeline *o = getObj(); ASSERT(o != nullptr);
        PlayerStatus s;
        bool ok = o->load(url, type,
            [this](const std::string &json) { publishJSON("pvevent", json); },
            s);
        if (!ok) info.postError(s.lastError.empty() ? "load failed" : s.lastError);
        else info.post(Bson::object{{"success", true}, {"status", statusToBson(s)}});
    } catch (const std::exception &e) {
        info.postError(e.what());
    }
}

void JSPlayerModule::playP(JQAsyncInfo &info)
{
    try {
        PlayerPipeline *o = getObj(); ASSERT(o != nullptr);
        PlayerStatus s;
        bool ok = o->play(s);
        if (!ok) info.postError("play failed: no pipeline");
        else info.post(Bson::object{{"success", true}, {"status", statusToBson(s)}});
    } catch (const std::exception &e) {
        info.postError(e.what());
    }
}

void JSPlayerModule::pauseP(JQAsyncInfo &info)
{
    try {
        PlayerPipeline *o = getObj(); ASSERT(o != nullptr);
        PlayerStatus s;
        bool ok = o->pause(s);
        info.post(Bson::object{{"success", ok}, {"status", statusToBson(s)}});
    } catch (const std::exception &e) {
        info.postError(e.what());
    }
}

void JSPlayerModule::resumeP(JQAsyncInfo &info)
{
    try {
        PlayerPipeline *o = getObj(); ASSERT(o != nullptr);
        PlayerStatus s;
        bool ok = o->resume(s);
        info.post(Bson::object{{"success", ok}, {"status", statusToBson(s)}});
    } catch (const std::exception &e) {
        info.postError(e.what());
    }
}

void JSPlayerModule::stopP(JQAsyncInfo &info)
{
    try {
        PlayerPipeline *o = getObj(); ASSERT(o != nullptr);
        PlayerStatus s;
        bool ok = o->stop(s);
        info.post(Bson::object{{"success", ok}, {"status", statusToBson(s)}});
    } catch (const std::exception &e) {
        info.postError(e.what());
    }
}

void JSPlayerModule::seekP(JQAsyncInfo &info)
{
    try {
        ASSERT(info.Length() == 1);
        ASSERT(info[0].is_object());
        double seconds = info[0]["seconds"].number_value();

        PlayerPipeline *o = getObj(); ASSERT(o != nullptr);
        PlayerStatus s;
        bool ok = o->seek(seconds, s);
        info.post(Bson::object{{"success", ok}, {"status", statusToBson(s)}});
    } catch (const std::exception &e) {
        info.postError(e.what());
    }
}

void JSPlayerModule::refreshP(JQAsyncInfo &info)
{
    try {
        PlayerPipeline *o = getObj(); ASSERT(o != nullptr);
        PlayerStatus s;
        bool ok = o->refresh(s);
        info.post(Bson::object{{"success", ok}, {"status", statusToBson(s)}});
    } catch (const std::exception &e) {
        info.postError(e.what());
    }
}