#include "jqutil_v2/jqutil.h"
#include "jsmodules/JSCModuleExtension.h"
#include "jquick_config.h"

using namespace JQUTIL_NS;

namespace bilinet {

extern void bilinet_init(JQModuleEnv* env);

static std::vector<std::string> exportList = {
    "bilinet"
};

static int module_init(JSContext *ctx, JSModuleDef *m)
{
    JQuick::sp<JQModuleEnv> env = JQModuleEnv::CreateModule(ctx, m, "bilinet");
    bilinet_init(env.get());
    env->setModuleExportDone(JS_UNDEFINED, exportList);
    return 0;
}

DEF_MODULE_LOAD_FUNC_EXPORT(bilinet, module_init, exportList)

}  // namespace bilinet

extern "C" JQUICK_EXPORT void custom_init_jsapis()
{
    registerCModuleLoader("bilinet", &bilinet::bilinet_module_load);
}
