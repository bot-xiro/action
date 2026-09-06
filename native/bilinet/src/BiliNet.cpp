#include "jqutil_v2/jqutil.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <syslog.h>

using namespace JQUTIL_NS;

namespace bilinet {

#define BN_LOG(fmt, ...) syslog(LOG_ERR, "[bilinet] " fmt, ##__VA_ARGS__)

// bilinet: 极简网络模块（通用 HTTP GET/POST，不做播放器）
//
// JS 侧使用：
//   import { bilinet } from 'bilinet'
//   const body = bilinet.httpGet(url, timeoutSec)             // 同步返回响应体
//   const body = bilinet.httpGet(url, timeoutSec, headers)    // headers: ["K: V", ...]
//   const body = bilinet.httpPost(url, postData, timeoutSec, headers)
//
// 背景（设备实测）：系统 http JSAPI 不发送自定义 header, UA/Referer 丢失后
// B 站风控接口（搜索等 wbi 接口）返回 v_voucher 空结果; 设备自带 /bin/curl
// 带浏览器 UA + Referer 后一切正常, 因此这里直接 popen 调 curl.
// v2: 增加自定义 headers (登录 Cookie 注入) 与 httpPost (评论发送).
// 安全: Cookie 头含 SESSDATA, 日志一律脱敏 (只打印键名不打印值).
class BiliNet : public JQUTIL_NS::JQBaseObject {
public:
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

        std::string headers = collectHeaders(ctx, info, 2);
        std::string cmd = "curl -s --compressed --max-time " + std::to_string(timeout)
            + " -A " + shellQuote(UA)
            + " -e " + shellQuote(REFERER)
            + headers
            + " " + shellQuote(url);
        BN_LOG("httpGet: %s", redactCurl(cmd).c_str());

        FILE* fp = popen(cmd.c_str(), "r");
        if (!fp) {
            BN_LOG("httpGet: popen failed");
            info.GetReturnValue().Set(std::string());
            return;
        }
        std::string body = drain(fp);
        int rc = pclose(fp);
        if (rc != 0) {
            BN_LOG("httpGet: curl rc=%d", rc);
        }
        BN_LOG("httpGet: len=%zu", body.size());
        info.GetReturnValue().Set(body);
    }

    // httpPost(url, postData, timeoutSec, headers) → 同步返回响应体（失败返回空串）
    // postData 为已编码的表单体 (application/x-www-form-urlencoded)
    void httpPost(JQUTIL_NS::JQFunctionInfo& info)
    {
        JSContext* ctx = info.GetContext();
        if (info.Length() < 2 || !JS_IsString(info[0]) || !JS_IsString(info[1])) {
            info.GetReturnValue().ThrowTypeError("httpPost: url/data required");
            return;
        }
        const char* urlC = JS_ToCString(ctx, info[0]);
        const char* dataC = JS_ToCString(ctx, info[1]);
        if (!urlC || !dataC) {
            info.GetReturnValue().ThrowTypeError("httpPost: invalid args");
            return;
        }
        std::string url(urlC);
        std::string data(dataC);
        JS_FreeCString(ctx, urlC);
        JS_FreeCString(ctx, dataC);
        if (url.size() > 2048 || data.size() > 4096 ||
            url.find_first_of("\r\n") != std::string::npos ||
            data.find_first_of("\r\n") != std::string::npos) {
            info.GetReturnValue().ThrowTypeError("httpPost: invalid url/data");
            return;
        }

        int timeout = 10;
        if (info.Length() >= 3 && JS_IsNumber(info[2])) {
            double t = 0;
            if (JS_ToFloat64(ctx, &t, info[2]) == 0 && t > 0 && t < 120) timeout = static_cast<int>(t);
        }
        if (timeout <= 0) timeout = 10;

        std::string headers = collectHeaders(ctx, info, 3);
        std::string cmd = "curl -s --compressed --max-time " + std::to_string(timeout)
            + " -X POST"
            + " -A " + shellQuote(UA)
            + " -e " + shellQuote(REFERER)
            + " -H " + shellQuote("Content-Type: application/x-www-form-urlencoded")
            + headers
            + " --data-binary " + shellQuote(data)
            + " " + shellQuote(url);
        BN_LOG("httpPost: %s", redactCurl(cmd).c_str());

        FILE* fp = popen(cmd.c_str(), "r");
        if (!fp) {
            BN_LOG("httpPost: popen failed");
            info.GetReturnValue().Set(std::string());
            return;
        }
        std::string body = drain(fp);
        int rc = pclose(fp);
        if (rc != 0) {
            BN_LOG("httpPost: curl rc=%d", rc);
        }
        BN_LOG("httpPost: len=%zu", body.size());
        info.GetReturnValue().Set(body);
    }

private:
    static const char* UA;
    static const char* REFERER;

    // shell 单引号转义 (URL/header/data 内置引号、& 等必须引住)
    static std::string shellQuote(const std::string& s)
    {
        std::string out = "'";
        for (char c : s) {
            if (c == '\'') out += "'\\''";
            else out += c;
        }
        out += "'";
        return out;
    }

    static std::string drain(FILE* fp)
    {
        std::string body;
        char buf[8192];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
            body.append(buf, n);
            if (body.size() > 4 * 1024 * 1024) break;  // 4MB 上限保护
        }
        return body;
    }

    // JS headers 数组 (["K: V", ...]) -> " -H 'K: V' -H ..." ; 非法/超限忽略.
    // Cookie 值进入命令行 (ps 可见) 是既有 UA 方案同级的暴露面 (单用户 root 设备).
    static std::string collectHeaders(JSContext* ctx, JQUTIL_NS::JQFunctionInfo& info, uint32_t idx)
    {
        std::string out;
        if (info.Length() <= idx || !JS_IsArray(ctx, info[idx])) return out;
        JSValue lenVal = JS_GetPropertyStr(ctx, info[idx], "length");
        double dlen = 0;
        bool has = JS_IsNumber(lenVal) != 0;
        if (has) JS_ToFloat64(ctx, &dlen, lenVal);
        JS_FreeValue(ctx, lenVal);
        if (!has) return out;
        uint32_t len = (uint32_t)dlen;
        if (len > 16) len = 16;
        for (uint32_t i = 0; i < len; i++) {
            JSValue item = JS_GetPropertyUint32(ctx, info[idx], i);
            if (JS_IsString(item)) {
                const char* h = JS_ToCString(ctx, item);
                if (h && std::strlen(h) > 3 && std::strlen(h) < 2048 &&
                    std::strpbrk(h, "\r\n\t") == NULL) {
                    out += " -H " + shellQuote(std::string(h));
                }
                if (h) JS_FreeCString(ctx, h);
            }
            JS_FreeValue(ctx, item);
        }
        return out;
    }

    // 日志脱敏: 命令行里 SESSDATA=xxx / bili_jct=xxx 的值替换为 ***
    static std::string redactCurl(const std::string& cmd)
    {
        std::string out = cmd;
        const char* keys[2] = { "SESSDATA=", "bili_jct=" };
        for (int k = 0; k < 2; k++) {
            size_t pos = out.find(keys[k]);
            while (pos != std::string::npos) {
                size_t end = pos + std::strlen(keys[k]);
                // 值到下一个 '&' 或空格或引号为止
                size_t vlen = 0;
                while (end + vlen < out.size() && out[end + vlen] != '&' &&
                       out[end + vlen] != ' ' && out[end + vlen] != '\'' &&
                       out[end + vlen] != ';' && vlen < 512) vlen++;
                out.replace(end, vlen, "***");
                pos = out.find(keys[k], end + 3);
            }
        }
        return out;
    }
};

const char* BiliNet::UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
const char* BiliNet::REFERER = "https://www.bilibili.com";

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
    tpl->SetProtoMethod("httpPost", &BiliNet::httpPost);
    return tpl->CallConstructor();
}

void bilinet_init(JQModuleEnv* env)
{
    env->setModuleExport("bilinet", createBiliNet(env));
}

}  // namespace bilinet
