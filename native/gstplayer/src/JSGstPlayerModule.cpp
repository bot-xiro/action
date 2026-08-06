// JSGstPlayerModule.cpp
// MiniApp JSAPI "gstplayer" 模块入口
// 与 WPE4YDPv2 的 jsapi_browser 同样使用 iot-miniapp-sdk 的 JQModuleEnv 模式

#include "jqutil_v2/jqutil.h"
#include "jsmodules/JSCModuleExtension.h"
#include "jquick_config.h"

using namespace JQUTIL_NS;

namespace gstplayer {

extern void gstplayer_init(JQModuleEnv* env);

static std::vector<std::string> exportList = {
    "gstPlayer"
};

static int module_init(JSContext *ctx, JSModuleDef *m)
{
    JQuick::sp<JQModuleEnv> env = JQModuleEnv::CreateModule(ctx, m, "gstplayer");
    gstplayer_init(env.get());
    env->setModuleExportDone(JS_UNDEFINED, exportList);
    return 0;
}

DEF_MODULE_LOAD_FUNC_EXPORT(gstplayer, module_init, exportList)

}  // namespace gstplayer

extern "C" JQUICK_EXPORT void custom_init_jsapis()
{
    registerCModuleLoader("gstplayer", &gstplayer::gstplayer_module_load);
}
