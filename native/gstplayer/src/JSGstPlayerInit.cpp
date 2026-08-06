// JSGstPlayerInit.cpp
// gstplayer 模块导出: gstPlayer 类

#include "jqutil_v2/jqutil.h"
#include "jsmodules/JSCModuleExtension.h"
#include "JSGstPlayer.h"

using namespace JQUTIL_NS;

namespace gstplayer {

static JSValue createGstPlayer(JQModuleEnv* env)
{
    JQFunctionTemplateRef tpl = JQFunctionTemplate::New(env, "gstPlayer");
    tpl->InstanceTemplate()->setObjectCreator([]() {
        static JSGstPlayer* player = []() {
            JSGstPlayer* instance = new JSGstPlayer();
            instance->REF();
            return instance;
        }();
        return player;
    });

    tpl->SetProtoMethod("open", &JSGstPlayer::open);
    tpl->SetProtoMethod("start", &JSGstPlayer::start);
    tpl->SetProtoMethod("pause", &JSGstPlayer::pause);
    tpl->SetProtoMethod("close", &JSGstPlayer::close);

    return tpl->CallConstructor();
}

void gstplayer_init(JQModuleEnv* env)
{
    env->setModuleExport("gstPlayer", createGstPlayer(env));
}

}  // namespace gstplayer