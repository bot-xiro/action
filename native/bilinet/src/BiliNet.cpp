#include "jqutil_v2/jqutil.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <syslog.h>

using namespace JQUTIL_NS;

namespace bilinet {

#define BN_LOG(fmt, ...) syslog(LOG_ERR, "[bilinet] " fmt, ##__VA_ARGS__)

// bilinet: 极简网络模块（只保留通用 HTTP GET，不做播放器）
//
// JS 侧使用：
//   import { bilinet } from 'bilinet'
//   const body = bilinet.httpGet(url, timeoutSec)   // 同步返回响应体字符串
//
// 背景（设备实测）：系统 http JSAPI 不发送自定义 header，UA/Referer 丢失后
// B 站风控接口（搜索等 wbi 接口）会返回 v_voucher 空结果；设备自带 /bin/curl
// 带浏览器 UA + Referer 后一切正常，因此这里直接 popen 调 curl。
class BiliNet : public JQUTIL_NS::JQBaseObject {
public:
    // httpGet(url, timeoutSec) → 同步返回响应体字符串（失败返回空串）
    void httpGet(JQUTIL_NS::JQFunctionInfo& info)
    {
        JSContext* ctx = info.GetContext();
        if (info.Length() < 1 || !JS_IsString(info[0])) {
            info.GetReturnValue().ThrowTypeError("httpGet: url required");
            return;
        }
        const char* urlC = JS_ToCString(ctx, info[0]);
        if (!urlC) {
            info.GetReturnValue().ThrowTypeError("httpGet: invalid url");
            return;
        }
        std::string url(urlC);
        JS_FreeCString(ctx, urlC);

        int timeout = 10;
        if (info.Length() >= 2 && JS_IsNumber(info[1])) {
            double t = 0;
            if (JS_ToFloat64(ctx, &t, info[1]) == 0 && t > 0 && t < 120) timeout = static_cast<int>(t);
        }
        if (timeout <= 0) timeout = 10;

        // 浏览器 UA + Referer（B站搜索/防盗链必需）
        std::string ua = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
        std::string referer = "https://www.bilibili.com";

        // shell 单引号转义（URL 内置 & 等必须引住）
        auto shellQuote = [](const std::string& s) {
            std::string out = "'";
            for (char c : s) {
                if (c == '\'') out += "'\\''";
                else out += c;
            }
            out += "'";
            return out;
        };

        std::string cmd = "curl -s --compressed --max-time " + std::to_string(timeout)
            + " -A " + shellQuote(ua)
            + " -e " + shellQuote(referer)
            + " " + shellQuote(url);
        BN_LOG("httpGet: %s", cmd.c_str());

        FILE* fp = popen(cmd.c_str(), "r");
        if (!fp) {
            BN_LOG("httpGet: popen failed");
            info.GetReturnValue().Set(std::string());
            return;
        }
        std::string body;
        char buf[8192];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
            body.append(buf, n);
            if (body.size() > 4 * 1024 * 1024) break;  // 4MB 上限保护
        }
        int rc = pclose(fp);
        if (rc != 0) {
            BN_LOG("httpGet: curl rc=%d", rc);
        }
        BN_LOG("httpGet: len=%zu", body.size());
        info.GetReturnValue().Set(body);
    }
};

static JSValue createBiliNet(JQModuleEnv* env)
{
    JQFunctionTemplateRef tpl = JQFunctionTemplate::New(env, "bilinet");
    tpl->InstanceTemplate()->setObjectCreator([]() {
        static BiliNet* instance = []() {
            BiliNet* p = new BiliNet();
            p->REF();
            return p;
        }();
        return instance;
    });
    tpl->SetProtoMethod("httpGet", &BiliNet::httpGet);
    return tpl->CallConstructor();
}

void bilinet_init(JQModuleEnv* env)
{
    env->setModuleExport("bilinet", createBiliNet(env));
}

}  // namespace bilinet
