// jsapi 模块入口: libjsapi_videoprobe.so 同名注册
// 三张名字保持一致: 模块名 "videoprobe", 常量 export "videoprobe",
// so 名 libjsapi_videoprobe.so
#include "jqutil_v2/jqutil.h"
#include "jsmodules/JSCModuleExtension.h"
#include "jquick_config.h"

using namespace JQUTIL_NS;

namespace videoprobe {

extern void videoprobe_init(JQModuleEnv* env);

static std::vector<std::string> exportList = { "videoprobe" };

static int module_init(JSContext* ctx, JSModuleDef* m) {
  JQuick::sp<JQModuleEnv> env = JQModuleEnv::CreateModule(ctx, m, "videoprobe");
  videoprobe_init(env.get());
  env->setModuleExportDone(JS_UNDEFINED, exportList);
  return 0;
}

DEF_MODULE_LOAD_FUNC_EXPORT(videoprobe, module_init, exportList)

}  // namespace videoprobe

extern "C" JQUICK_EXPORT void custom_init_jsapis() {
  registerCModuleLoader("videoprobe", &videoprobe::videoprobe_module_load);
}
