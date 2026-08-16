#include "jqutil_v2/jqutil.h"
#include "jsmodules/JSCModuleExtension.h"
#include "jquick_config.h"
#include "BiliStreamPlayer.h"

using namespace JQUTIL_NS;

namespace bilistream {

extern void bilistream_init(JQModuleEnv* env);

static std::vector<std::string> exportList = {
    "biliStream"
};

static int module_init(JSContext *ctx, JSModuleDef *m)
{
    JQuick::sp<JQModuleEnv> env = JQModuleEnv::CreateModule(ctx, m, "bilistream");
    bilistream_init(env.get());
    env->setModuleExportDone(JS_UNDEFINED, exportList);
    return 0;
}

DEF_MODULE_LOAD_FUNC_EXPORT(bilistream, module_init, exportList)

} // namespace bilistream

extern "C" JQUICK_EXPORT void custom_init_jsapis()
{
    registerCModuleLoader("bilistream", &bilistream::bilistream_module_load);
}
