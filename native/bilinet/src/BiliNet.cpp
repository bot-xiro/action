#include "jqutil_v2/jqutil.h"

#include <cstdio>
#include <thread>
#include <mutex>
#include <cstring>
#include <string>
#include <syslog.h>
#include <sys/stat.h>
#include <errno.h>
#include <dlfcn.h>

using namespace JQUTIL_NS;

namespace bilinet {

#define BN_LOG(fmt, ...) syslog(LOG_ERR, "[bilinet] " fmt, ##__VA_ARGS__)

// ----------------------------------------------------------------------
// sqlite3 动态绑定
//
// 为什么 dlopen 而不是直接链接 / 用系统的 sqlite3 JSAPI:
//   1) 交叉编译工具链里没有 sqlite3.h, 直接链接无从下手;
//   2) import 'sqlite3' 这个 JSAPI 是否存在无法在编译期确认, 一旦缺失整个
//      app 会在模块解析阶段起不来 —— 风险太大;
//   3) 设备 /usr/lib 下确实有 libsqlite3.so.0 (miniapp 进程本身就在用),
//      dlopen 失败只是这一个能力不可用, 上层可以退回 JSON 文件兜底.
// ----------------------------------------------------------------------
struct sqlite3;

namespace sql {
    typedef int (*fn_open)(const char* filename, sqlite3** ppDb);
    typedef int (*fn_exec)(sqlite3* db, const char* sql,
                           int (*cb)(void*, int, char**, char**), void* arg, char** errmsg);
    typedef int (*fn_close)(sqlite3* db);
    typedef const char* (*fn_errmsg)(sqlite3* db);
    typedef void (*fn_free)(void* p);

    static void* handle = NULL;
    static fn_open  p_open = NULL;
    static fn_exec  p_exec = NULL;
    static fn_close p_close = NULL;
    static fn_errmsg p_errmsg = NULL;
    static fn_free  p_free = NULL;
    static bool tried = false;

    static bool load()
    {
        if (handle) return true;
        if (tried) return false;
        tried = true;
        const char* names[3] = { "libsqlite3.so.0", "libsqlite3.so", NULL };
        for (int i = 0; names[i]; i++) {
            void* h = dlopen(names[i], RTLD_NOW | RTLD_GLOBAL);
            if (!h) continue;
            fn_open o = (fn_open)dlsym(h, "sqlite3_open");
            fn_exec e = (fn_exec)dlsym(h, "sqlite3_exec");
            fn_close c = (fn_close)dlsym(h, "sqlite3_close");
            fn_errmsg m = (fn_errmsg)dlsym(h, "sqlite3_errmsg");
            fn_free  f = (fn_free)dlsym(h, "sqlite3_free");
            if (o && e && c && m && f) {
                handle = h; p_open = o; p_exec = e; p_close = c; p_errmsg = m; p_free = f;
                return true;
            }
            dlclose(h);
        }
        BN_LOG("sqlite3 dlopen failed");
        return false;
    }

    static sqlite3* db = NULL;

    static void closeDb()
    {
        if (db && p_close) p_close(db);
        db = NULL;
    }

    // JSON 字符串转义 (UTF-8 原样透传, 只处理控制字符与引号/反斜杠)
    static std::string jsonStr(const char* s)
    {
        std::string out = "\"";
        if (s) {
            for (const char* p = s; *p; p++) {
                unsigned char c = (unsigned char)*p;
                if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
                else if (c == '\n') out += "\\n";
                else if (c == '\r') out += "\\r";
                else if (c == '\t') out += "\\t";
                else if (c < 0x20) out += ' ';
                else out += (char)c;
            }
        }
        out += "\"";
        return out;
    }

    struct QueryCtx {
        std::string out;
        bool firstRow;
        explicit QueryCtx() : firstRow(true) { out = "["; }
    };

    static int queryCb(void* arg, int argc, char** argv, char** colName)
    {
        QueryCtx* c = static_cast<QueryCtx*>(arg);
        if (!c->firstRow) c->out += ",";
        c->firstRow = false;
        c->out += "{";
        for (int i = 0; i < argc; i++) {
            if (i) c->out += ",";
            c->out += jsonStr(colName && colName[i] ? colName[i] : "");
            c->out += ":";
            c->out += jsonStr(argv && argv[i] ? argv[i] : "");
        }
        c->out += "}";
        return 0;
    }
}  // namespace sql

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
// v6: 增加异步版 httpGetAsync/httpPostAsync (Promise, 工作线程执行, 不阻塞 JS 主线程).
// 安全: Cookie 头含 SESSDATA, 日志一律脱敏 (只打印键名不打印值).
// v6.1: popen/pclose 在多线程并发调用下有隐患 (内部子进程表 / 回收竞争), 因此异步请求虽然
// 跑在工作线程, curl 的执行仍然串行化 —— JS 主线程不受影响, 只是并发请求排队.
// 注意: 必须定义在 class BiliNet 之前 (成员函数 runAsync 里要用).
static std::mutex g_curlMutex;

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

    // ---- v6: 异步 HTTP (Promise) -------------------------------------------
    // 背景: 同步 httpGet/httpPost 阻塞 QuickJS 主线程 (curl 最长 timeout 秒), 页面渲染与触摸全卡住.
    // 异步版把 curl 丢到工作线程, 完成后 post 回 JS 线程 resolve:
    //   const body = await bilinet.httpGetAsync(url, timeoutSec, headers?)
    //   const body = await bilinet.httpPostAsync(url, data, timeoutSec, headers?)
    // body 与同步版一致 = curl 原始响应体字符串 (JS 侧自己 JSON.parse).
    void httpGetAsync(JQUTIL_NS::JQAsyncInfo& info)
    {
        if (info.Length() < 1 || !info[0].is_string()) {
            info.postError("httpGetAsync: url required");
            return;
        }
        std::string url = info[0].string_value();
        if (url.size() > 2048 || url.find_first_of("\r\n") != std::string::npos) {
            info.postError("httpGetAsync: invalid url");
            return;
        }
        int timeout = 10;
        if (info.Length() >= 2 && info[1].is_number()) {
            int t = info[1].int_value();
            if (t > 0 && t < 120) timeout = t;
        }
        std::string headers = headersFromBson(info, 2);
        std::string cmd = "curl -s --compressed --max-time " + std::to_string(timeout)
            + " -A " + shellQuote(UA)
            + " -e " + shellQuote(REFERER)
            + headers
            + " " + shellQuote(url);
        runAsync(info, cmd, "httpGetAsync");
    }

    void httpPostAsync(JQUTIL_NS::JQAsyncInfo& info)
    {
        if (info.Length() < 2 || !info[0].is_string() || !info[1].is_string()) {
            info.postError("httpPostAsync: url/data required");
            return;
        }
        std::string url = info[0].string_value();
        std::string data = info[1].string_value();
        if (url.size() > 2048 || data.size() > 4096 ||
            url.find_first_of("\r\n") != std::string::npos ||
            data.find_first_of("\r\n") != std::string::npos) {
            info.postError("httpPostAsync: invalid url/data");
            return;
        }
        int timeout = 10;
        if (info.Length() >= 3 && info[2].is_number()) {
            int t = info[2].int_value();
            if (t > 0 && t < 120) timeout = t;
        }
        std::string headers = headersFromBson(info, 3);
        std::string cmd = "curl -s --compressed --max-time " + std::to_string(timeout)
            + " -X POST"
            + " -A " + shellQuote(UA)
            + " -e " + shellQuote(REFERER)
            + " -H " + shellQuote("Content-Type: application/x-www-form-urlencoded")
            + headers
            + " --data-binary " + shellQuote(data)
            + " " + shellQuote(url);
        runAsync(info, cmd, "httpPostAsync");
    }


private:
    // 异步方法的 headers 解析 (异步侧只有 Bson, 拿不到 JSContext)
    static std::string headersFromBson(JQUTIL_NS::JQAsyncInfo& info, uint32_t idx)
    {
        std::string out;
        if (info.Length() <= idx || !info[idx].is_array()) return out;
        const Bson::array& arr = info[idx].array_items();
        for (size_t i = 0; i < arr.size(); i++) {
            if (!arr[i].is_string()) continue;
            std::string h = arr[i].string_value();
            if (h.find_first_of("\r\n") != std::string::npos) continue;
            out += " -H " + shellQuote(h);
        }
        return out;
    }

    // 工作线程跑 curl, 完成后把结果 post 回 JS 线程 (post/postError 跨线程安全)
    static void runAsync(JQUTIL_NS::JQAsyncInfo& info, const std::string& cmd, const char* tag)
    {
        BN_LOG("%s: %s", tag, redactCurl(cmd).c_str());
        std::string cmdCopy = cmd;
        std::string tagCopy = tag;
        try {
            JQUTIL_NS::JQAsyncInfo ainfo = info;   // 值拷贝: 供工作线程投递
            std::thread([ainfo, cmdCopy, tagCopy]() mutable {
                std::string body;
                int rc = 0;
                {
                    std::lock_guard<std::mutex> lock(g_curlMutex);
                    FILE* fp = popen(cmdCopy.c_str(), "r");
                    if (!fp) { ainfo.postError(tagCopy + ": curl 启动失败"); return; }
                    body = drain(fp);
                    rc = pclose(fp);
                }
                BN_LOG("%s: rc=%d len=%zu", tagCopy.c_str(), rc, body.size());
                if (body.empty()) {
                    ainfo.postError(tagCopy + ": 空响应 (rc=" + std::to_string(rc) + ")");
                    return;
                }
                ainfo.post(Bson(body));
            }).detach();
        } catch (...) {
            info.postError(std::string(tag) + ": 线程创建失败");
        }
    }


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

public:
    // ------------------------------------------------------------------
    // v3: 文件读写 —— 供运行日志与登录信息落盘使用
    //
    // 为什么需要: 系统 fs JSAPI 只暴露 readdir/stat/exists/readFile/mkdir/rm,
    // 没有写入接口, 且限定在应用 data 目录; 而需求要求日志写到
    // /userdisk/xiro/bilibili.log、数据库放 /userdisk/xiro/, 这里直接用
    // libc 打开绝对路径, 不受该限制.
    //
    // JS 侧:
    //   const text = bilinet.readFile(path)          // 失败返回 ''
    //   const ok   = bilinet.writeFile(path, data)   // 覆盖写
    //   const ok   = bilinet.writeFile(path, data, true)  // 追加写
    //   const ok   = bilinet.mkdirs('/a/b/c')        // 逐级创建, 已存在算成功
    //   const ex   = bilinet.fileExists(path)
    // ------------------------------------------------------------------

    // readFile(path) → 文本; 打不开返回空串
    void readFile(JQUTIL_NS::JQFunctionInfo& info)
    {
        JSContext* ctx = info.GetContext();
        if (info.Length() < 1 || !JS_IsString(info[0])) {
            info.GetReturnValue().ThrowTypeError("readFile: path required");
            return;
        }
        const char* p = JS_ToCString(ctx, info[0]);
        if (!p) { info.GetReturnValue().Set(std::string()); return; }
        std::string path(p);
        JS_FreeCString(ctx, p);

        std::string body;
        FILE* fp = fopen(path.c_str(), "rb");
        if (!fp) {
            BN_LOG("readFile: open failed: %s", path.c_str());
            info.GetReturnValue().Set(std::string());
            return;
        }
        char buf[8192];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) body.append(buf, n);
        fclose(fp);
        info.GetReturnValue().Set(body);
    }

    // writeFile(path, data, append) → bool
    void writeFile(JQUTIL_NS::JQFunctionInfo& info)
    {
        JSContext* ctx = info.GetContext();
        if (info.Length() < 2 || !JS_IsString(info[0]) || !JS_IsString(info[1])) {
            info.GetReturnValue().ThrowTypeError("writeFile: path/data required");
            return;
        }
        const char* p = JS_ToCString(ctx, info[0]);
        const char* d = JS_ToCString(ctx, info[1]);
        if (!p || !d) {
            if (p) JS_FreeCString(ctx, p);
            if (d) JS_FreeCString(ctx, d);
            info.GetReturnValue().Set(false);
            return;
        }
        std::string path(p), data(d);
        JS_FreeCString(ctx, p);
        JS_FreeCString(ctx, d);

        bool append = false;
        if (info.Length() >= 3) append = (JS_ToBool(ctx, info[2]) != 0);

        FILE* fp = fopen(path.c_str(), append ? "ab" : "wb");
        if (!fp) {
            BN_LOG("writeFile: open failed: %s", path.c_str());
            info.GetReturnValue().Set(false);
            return;
        }
        size_t w = fwrite(data.data(), 1, data.size(), fp);
        fclose(fp);
        info.GetReturnValue().Set(w == data.size());
    }

    // mkdirs(path) → bool; 逐级创建, 中间目录已存在不算失败
    void mkdirs(JQUTIL_NS::JQFunctionInfo& info)
    {
        JSContext* ctx = info.GetContext();
        if (info.Length() < 1 || !JS_IsString(info[0])) {
            info.GetReturnValue().ThrowTypeError("mkdirs: path required");
            return;
        }
        const char* p = JS_ToCString(ctx, info[0]);
        if (!p) { info.GetReturnValue().Set(false); return; }
        std::string path(p);
        JS_FreeCString(ctx, p);
        if (path.empty()) { info.GetReturnValue().Set(false); return; }

        std::string cur;
        for (size_t i = 0; i < path.size(); i++) {
            if (path[i] == '/' && i > 0) {
                if (mkdir(cur.c_str(), 0775) != 0 && errno != EEXIST) {
                    BN_LOG("mkdirs: failed at %s (errno=%d)", cur.c_str(), errno);
                    info.GetReturnValue().Set(false);
                    return;
                }
            }
            cur.push_back(path[i]);
        }
        if (mkdir(cur.c_str(), 0775) != 0 && errno != EEXIST) {
            BN_LOG("mkdirs: failed at %s (errno=%d)", cur.c_str(), errno);
            info.GetReturnValue().Set(false);
            return;
        }
        info.GetReturnValue().Set(true);
    }

    // fileExists(path) → bool
    void fileExists(JQUTIL_NS::JQFunctionInfo& info)
    {
        JSContext* ctx = info.GetContext();
        if (info.Length() < 1 || !JS_IsString(info[0])) {
            info.GetReturnValue().ThrowTypeError("fileExists: path required");
            return;
        }
        const char* p = JS_ToCString(ctx, info[0]);
        if (!p) { info.GetReturnValue().Set(false); return; }
        std::string path(p);
        JS_FreeCString(ctx, p);
        struct stat st;
        info.GetReturnValue().Set(stat(path.c_str(), &st) == 0);
    }

    // ------------------------------------------------------------------
    // v4: sqlite3 —— 登录内容落库 (/userdisk/xiro/bilibili.db)
    //
    // JS 侧:
    //   const ok  = bilinet.dbOpen('/userdisk/xiro/bilibili.db')
    //   const ok  = bilinet.dbExec('CREATE TABLE IF NOT EXISTS ...')
    //   const js  = bilinet.dbQuery('SELECT * FROM auth')   // JSON 数组字符串
    //   bilinet.dbClose()
    //
    // 说明: 没有用预处理语句, SQL 由 JS 侧拼好后整体交给 sqlite3_exec;
    // 字符串字面量里的单引号必须先替换成 '' (store.js 的 q() 会做这件事)。
    // ------------------------------------------------------------------

    static bool sqlReady()
    {
        return sql::load();
    }

    // dbOpen(path) → bool; 已打开会先关掉旧的
    void dbOpen(JQUTIL_NS::JQFunctionInfo& info)
    {
        JSContext* ctx = info.GetContext();
        if (info.Length() < 1 || !JS_IsString(info[0])) {
            info.GetReturnValue().ThrowTypeError("dbOpen: path required");
            return;
        }
        const char* p = JS_ToCString(ctx, info[0]);
        if (!p) { info.GetReturnValue().Set(false); return; }
        std::string path(p);
        JS_FreeCString(ctx, p);

        if (!sqlReady()) { info.GetReturnValue().Set(false); return; }
        sql::closeDb();
        int rc = sql::p_open(path.c_str(), &sql::db);
        if (rc != 0) {
            BN_LOG("dbOpen: rc=%d path=%s", rc, path.c_str());
            sql::db = NULL;
            info.GetReturnValue().Set(false);
            return;
        }
        info.GetReturnValue().Set(true);
    }

    // dbExec(sql) → bool
    void dbExec(JQUTIL_NS::JQFunctionInfo& info)
    {
        JSContext* ctx = info.GetContext();
        if (info.Length() < 1 || !JS_IsString(info[0])) {
            info.GetReturnValue().ThrowTypeError("dbExec: sql required");
            return;
        }
        const char* s = JS_ToCString(ctx, info[0]);
        if (!s) { info.GetReturnValue().Set(false); return; }
        std::string sqlText(s);
        JS_FreeCString(ctx, s);

        if (!sqlReady() || !sql::db) { info.GetReturnValue().Set(false); return; }
        char* err = NULL;
        int rc = sql::p_exec(sql::db, sqlText.c_str(), NULL, NULL, &err);
        if (rc != 0) {
            BN_LOG("dbExec: rc=%d err=%s", rc, err ? err : "");
            if (err && sql::p_free) sql::p_free(err);
            info.GetReturnValue().Set(false);
            return;
        }
        info.GetReturnValue().Set(true);
    }

    // dbQuery(sql) → JSON 数组字符串 (查询失败返回 '[]')
    void dbQuery(JQUTIL_NS::JQFunctionInfo& info)
    {
        JSContext* ctx = info.GetContext();
        if (info.Length() < 1 || !JS_IsString(info[0])) {
            info.GetReturnValue().ThrowTypeError("dbQuery: sql required");
            return;
        }
        const char* s = JS_ToCString(ctx, info[0]);
        if (!s) { info.GetReturnValue().Set(std::string("[]")); return; }
        std::string sqlText(s);
        JS_FreeCString(ctx, s);

        if (!sqlReady() || !sql::db) { info.GetReturnValue().Set(std::string("[]")); return; }
        sql::QueryCtx c;
        char* err = NULL;
        int rc = sql::p_exec(sql::db, sqlText.c_str(), sql::queryCb, &c, &err);
        if (rc != 0) {
            BN_LOG("dbQuery: rc=%d err=%s", rc, err ? err : "");
            if (err && sql::p_free) sql::p_free(err);
            info.GetReturnValue().Set(std::string("[]"));
            return;
        }
        c.out += "]";
        info.GetReturnValue().Set(c.out);
    }

    // dbClose() → bool
    void dbClose(JQUTIL_NS::JQFunctionInfo& info)
    {
        if (!sql::db) { info.GetReturnValue().Set(true); return; }
        sql::closeDb();
        info.GetReturnValue().Set(true);
    }

    // ------------------------------------------------------------------
    // v5: exec(cmd) —— 执行单条设备 shell 命令, 同步返回 stdout (失败返回 '')
    //
    // 用途: 播放时防息屏 —— 息屏是输入事件空闲计时器, 播放中定期注入
    // `send_event touch move ...` (无副作用, 不会触发点击) 重置计时器.
    // JS 侧:
    //   const out = bilinet.exec('send_event touch move 240 479')
    // 安全: 命令只由本应用打包的 JS 发起 (单用户自有设备), 长度限 512,
    // 拒绝控制字符; 经 popen(/bin/sh -c) 执行.
    // ------------------------------------------------------------------
    void exec(JQUTIL_NS::JQFunctionInfo& info)
    {
        JSContext* ctx = info.GetContext();
        if (info.Length() < 1 || !JS_IsString(info[0])) {
            info.GetReturnValue().Set(std::string());
            return;
        }
        const char* c = JS_ToCString(ctx, info[0]);
        if (!c) { info.GetReturnValue().Set(std::string()); return; }
        std::string cmd(c);
        JS_FreeCString(ctx, c);
        if (cmd.empty() || cmd.size() > 512 ||
            cmd.find_first_of("\r\n") != std::string::npos) {
            BN_LOG("exec: invalid cmd (len=%zu)", cmd.size());
            info.GetReturnValue().Set(std::string());
            return;
        }
        BN_LOG("exec: %s", cmd.c_str());
        FILE* fp = popen(cmd.c_str(), "r");
        if (!fp) { info.GetReturnValue().Set(std::string()); return; }
        std::string out;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
            out.append(buf, n);
            if (out.size() > 64 * 1024) break;  // 64KB 上限
        }
        pclose(fp);
        info.GetReturnValue().Set(out);
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
    tpl->SetProtoMethod("readFile", &BiliNet::readFile);
    tpl->SetProtoMethod("writeFile", &BiliNet::writeFile);
    tpl->SetProtoMethod("mkdirs", &BiliNet::mkdirs);
    tpl->SetProtoMethod("fileExists", &BiliNet::fileExists);
    tpl->SetProtoMethod("dbOpen", &BiliNet::dbOpen);
    tpl->SetProtoMethod("dbExec", &BiliNet::dbExec);
    tpl->SetProtoMethod("dbQuery", &BiliNet::dbQuery);
    tpl->SetProtoMethod("dbClose", &BiliNet::dbClose);
    tpl->SetProtoMethod("exec", &BiliNet::exec);
    tpl->SetProtoMethodPromise("httpGetAsync", &BiliNet::httpGetAsync);
    tpl->SetProtoMethodPromise("httpPostAsync", &BiliNet::httpPostAsync);
    return tpl->CallConstructor();
}

void bilinet_init(JQModuleEnv* env)
{
    env->setModuleExport("bilinet", createBiliNet(env));
}

}  // namespace bilinet
