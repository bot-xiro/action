// ============================================================================
//  播放器 JSAPI 总注册入口（全新 plugin: pvplayer）
//  规则：pluginname == .so 去掉 libjsapi_ 前缀 == registerCModuleLoader 第一参
//       == JS import 'pvplayer' 里的名字
//  产物：libjsapi_pvplayer.so；JS 端 import { PlayerModule } from 'pvplayer'
// ============================================================================

#include <jsmodules/JSCModuleExtension.h>
#include <jquick_config.h>
#include "PlayerModule/JSPlayerModule.hpp"

using namespace JQUTIL_NS;

static std::vector<std::string> exportList = {
    "PlayerModule",
};

static int module_init(JSContext *ctx, JSModuleDef *m)
{
    auto env = JQModuleEnv::CreateModule(ctx, m, "pvplayer");
    env->setModuleExport("PlayerModule", createPlayerModule(env.get()));
    env->setModuleExportDone(JS_UNDEFINED, exportList);
    return 0;
}

DEF_MODULE_LOAD_FUNC_EXPORT(pvplayer, module_init, exportList)

extern "C" JQUICK_EXPORT void custom_init_jsapis()
{
    registerCModuleLoader("pvplayer", &pvplayer_module_load);
}