#include "jqutil_v2/jqutil.h"
#include "jsmodules/JSCModuleExtension.h"
#include "jquick_config.h"
#include "BiliAuth.h"

using namespace JQUTIL_NS;

namespace biliauth {

extern void biliauth_init(JQModuleEnv* env);

static std::vector<std::string> exportList = {
    "biliAuth"
};

static int module_init(JSContext *ctx, JSModuleDef *m)
{
    JQuick::sp<JQModuleEnv> env = JQModuleEnv::CreateModule(ctx, m, "bili-auth");
    biliauth_init(env.get());
    env->setModuleExportDone(JS_UNDEFINED, exportList);
    return 0;
}

DEF_MODULE_LOAD_FUNC_EXPORT(bili_auth, module_init, exportList)

}  // namespace biliauth

extern "C" JQUICK_EXPORT void custom_init_jsapis()
{
    registerCModuleLoader("bili-auth", &biliauth::biliauth_module_load);
}