#include "jqutil_v2/jqutil.h"
#include "jsmodules/JSCModuleExtension.h"
#include "jquick_config.h"

using namespace JQUTIL_NS;

namespace imageviewer {

extern void imageviewer_init(JQModuleEnv* env);

static std::vector<std::string> exportList = {
    "imageviewer"
};

static int module_init(JSContext *ctx, JSModuleDef *m)
{
    JQuick::sp<JQModuleEnv> env = JQModuleEnv::CreateModule(ctx, m, "imageviewer");
    imageviewer_init(env.get());
    env->setModuleExportDone(JS_UNDEFINED, exportList);
    return 0;
}

DEF_MODULE_LOAD_FUNC_EXPORT(imageviewer, module_init, exportList)

}  // namespace imageviewer

extern "C" JQUICK_EXPORT void custom_init_jsapis()
{
    registerCModuleLoader("imageviewer", &imageviewer::imageviewer_module_load);
}
