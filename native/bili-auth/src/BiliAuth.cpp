#include "BiliAuth.h"

#include <syslog.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>
#include <ctime>

#define AUTH_LOG(fmt, ...) syslog(LOG_LOCAL7 | LOG_ERR, "[bili-auth] " fmt, ##__VA_ARGS__)

namespace biliauth {

const char* BiliAuth::DB_PATH = "/userdisk/xiro/bili/app.db";

BiliAuth::BiliAuth() = default;

BiliAuth::~BiliAuth() {
    closeDB();
}

bool BiliAuth::openDB() {
    if (inited_.load() && db_) return true;

    std::lock_guard<std::mutex> lk(dbMutex_);
    if (inited_.load() && db_) return true;

    // 确保目录存在
    if (mkdir("/userdisk/xiro", 0755) != 0 && errno != EEXIST) {
        AUTH_LOG("mkdir /userdisk/xiro failed: %s", strerror(errno));
    }
    if (mkdir("/userdisk/xiro/bili", 0755) != 0 && errno != EEXIST) {
        AUTH_LOG("mkdir /userdisk/xiro/bili failed: %s", strerror(errno));
    }

    int rc = sqlite3_open(DB_PATH, &db_);
    if (rc != SQLITE_OK) {
        AUTH_LOG("sqlite3_open failed: %s", sqlite3_errmsg(db_));
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
        return false;
    }

    // 启用 WAL 模式提升并发性能
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    // 创建表
    const char* createCookies = 
        "CREATE TABLE IF NOT EXISTS cookies ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT NOT NULL,"
        "  updated_at INTEGER NOT NULL"
        ");";
    const char* createSettings = 
        "CREATE TABLE IF NOT EXISTS settings ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT NOT NULL,"
        "  updated_at INTEGER NOT NULL"
        ");";
    const char* createUserInfo = 
        "CREATE TABLE IF NOT EXISTS user_info ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT NOT NULL,"
        "  updated_at INTEGER NOT NULL"
        ");";

    if (!execSQL(createCookies) || !execSQL(createSettings) || !execSQL(createUserInfo)) {
        closeDB();
        return false;
    }

    inited_.store(true);
    AUTH_LOG("database initialized at %s", DB_PATH);
    return true;
}

void BiliAuth::closeDB() {
    std::lock_guard<std::mutex> lk(dbMutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
        inited_.store(false);
    }
}

bool BiliAuth::execSQL(const char* sql) {
    if (!db_) return false;
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        AUTH_LOG("SQL exec failed: %s (sql: %s)", errMsg ? errMsg : "unknown", sql);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool BiliAuth::querySingle(const char* sql, std::string& outValue) {
    if (!db_) return false;
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        AUTH_LOG("sqlite3_prepare failed: %s", sqlite3_errmsg(db_));
        return false;
    }
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* val = sqlite3_column_text(stmt, 0);
        if (val) {
            outValue = reinterpret_cast<const char*>(val);
            found = true;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

bool BiliAuth::queryRow(const char* sql, std::string& outKey, std::string& outValue) {
    if (!db_) return false;
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        AUTH_LOG("sqlite3_prepare failed: %s", sqlite3_errmsg(db_));
        return false;
    }
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* k = sqlite3_column_text(stmt, 0);
        const unsigned char* v = sqlite3_column_text(stmt, 1);
        if (k && v) {
            outKey = reinterpret_cast<const char*>(k);
            outValue = reinterpret_cast<const char*>(v);
            found = true;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

// ---- JS 方法实现 ----

void BiliAuth::init(JQUTIL_NS::JQFunctionInfo& info) {
    bool ok = openDB();
    info.GetReturnValue().Set(ok);
}

void BiliAuth::getCookie(JQUTIL_NS::JQFunctionInfo& info) {
    if (!openDB()) { info.GetReturnValue().Set(std::string()); return; }

    std::string cookie;
    if (querySingle("SELECT value FROM cookies WHERE key='bili_cookie';", cookie)) {
        info.GetReturnValue().Set(cookie);
    } else {
        info.GetReturnValue().Set(std::string());
    }
}

void BiliAuth::setCookie(JQUTIL_NS::JQFunctionInfo& info) {
    JSContext* ctx = info.GetContext();
    if (info.Length() < 1 || !JS_IsString(info[0])) {
        info.GetReturnValue().ThrowTypeError("setCookie: string required");
        return;
    }

    if (!openDB()) { info.GetReturnValue().Set(false); return; }

    const char* cookieC = JS_ToCString(ctx, info[0]);
    std::string cookie = cookieC ? cookieC : "";
    JS_FreeCString(ctx, cookieC);

    long long now = time(nullptr);
    char sql[4096];
    snprintf(sql, sizeof(sql),
        "INSERT OR REPLACE INTO cookies (key, value, updated_at) VALUES ('bili_cookie', '%s', %lld);",
        sqlite3_mprintf("%q", cookie.c_str()), now);

    bool ok = execSQL(sql);
    info.GetReturnValue().Set(ok);
}

void BiliAuth::clearCookie(JQUTIL_NS::JQFunctionInfo& info) {
    if (!openDB()) { info.GetReturnValue().Set(false); return; }
    bool ok = execSQL("DELETE FROM cookies WHERE key='bili_cookie';");
    info.GetReturnValue().Set(ok);
}

void BiliAuth::getSettings(JQUTIL_NS::JQFunctionInfo& info) {
    if (!openDB()) { info.GetReturnValue().Set(std::string("{}")); return; }

    JSContext* ctx = info.GetContext();
    JSValue obj = JS_NewObject(ctx);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT key, value FROM settings;";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* k = sqlite3_column_text(stmt, 0);
            const unsigned char* v = sqlite3_column_text(stmt, 1);
            if (k && v) {
                JSValue val = JS_NewString(ctx, reinterpret_cast<const char*>(v));
                JS_SetPropertyStr(ctx, obj, reinterpret_cast<const char*>(k), val);
            }
        }
        sqlite3_finalize(stmt);
    }
    info.GetReturnValue().Set(obj);
}

void BiliAuth::setSetting(JQUTIL_NS::JQFunctionInfo& info) {
    JSContext* ctx = info.GetContext();
    if (info.Length() < 2 || !JS_IsString(info[0]) || !JS_IsString(info[1])) {
        info.GetReturnValue().ThrowTypeError("setSetting: key, value strings required");
        return;
    }

    if (!openDB()) { info.GetReturnValue().Set(false); return; }

    const char* keyC = JS_ToCString(ctx, info[0]);
    const char* valC = JS_ToCString(ctx, info[1]);
    std::string key = keyC ? keyC : "";
    std::string val = valC ? valC : "";
    JS_FreeCString(ctx, keyC);
    JS_FreeCString(ctx, valC);

    long long now = time(nullptr);
    char sql[4096];
    snprintf(sql, sizeof(sql),
        "INSERT OR REPLACE INTO settings (key, value, updated_at) VALUES ('%s', '%s', %lld);",
        sqlite3_mprintf("%q", key.c_str()), sqlite3_mprintf("%q", val.c_str()), now);

    bool ok = execSQL(sql);
    info.GetReturnValue().Set(ok);
}

// 生成登录二维码
void BiliAuth::generateQrCode(JQUTIL_NS::JQFunctionInfo& info) {
    // 调用设备 curl 获取二维码
    const char* url = "https://passport.bilibili.com/x/passport-login/web/qrcode/generate?source=main-fe-header";
    
    std::string ua = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
    std::string referer = "https://www.bilibili.com";

    // shell 单引号转义
    auto shellQuote = [](const std::string& s) {
        std::string out = "'";
        for (char c : s) {
            if (c == '\'') out += "'\\''";
            else out += c;
        }
        out += "'";
        return out;
    };

    std::string cmd = "curl -s --compressed --max-time 10 "
        + shellQuote(std::string("-A ") + shellQuote(ua))
        + " " + shellQuote(std::string("-e ") + shellQuote(referer))
        + " " + shellQuote(url);

    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) {
        AUTH_LOG("generateQrCode: popen failed");
        info.GetReturnValue().Set(std::string("{}"));
        return;
    }

    std::string body;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        body.append(buf, n);
        if (body.size() > 1024 * 1024) break;
    }
    pclose(fp);

    info.GetReturnValue().Set(body);
}

// 轮询二维码登录状态
void BiliAuth::pollQrCode(JQUTIL_NS::JQFunctionInfo& info) {
    JSContext* ctx = info.GetContext();
    if (info.Length() < 1 || !JS_IsString(info[0])) {
        info.GetReturnValue().ThrowTypeError("pollQrCode: qrcodeKey string required");
        return;
    }

    const char* keyC = JS_ToCString(ctx, info[0]);
    std::string qrcodeKey = keyC ? keyC : "";
    JS_FreeCString(ctx, keyC);

    if (qrcodeKey.empty()) {
        info.GetReturnValue().Set(std::string("{}"));
        return;
    }

    std::string url = "https://passport.bilibili.com/x/passport-login/web/qrcode/poll?qrcode_key=" 
        + qrcodeKey + "&source=main-fe-header";

    std::string ua = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
    std::string referer = "https://www.bilibili.com";

    auto shellQuote = [](const std::string& s) {
        std::string out = "'";
        for (char c : s) {
            if (c == '\'') out += "'\\''";
            else out += c;
        }
        out += "'";
        return out;
    };

    std::string cmd = "curl -s --compressed --max-time 10 "
        + shellQuote(std::string("-A ") + shellQuote(ua))
        + " " + shellQuote(std::string("-e ") + shellQuote(referer))
        + " " + shellQuote(url);

    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) {
        AUTH_LOG("pollQrCode: popen failed");
        info.GetReturnValue().Set(std::string("{}"));
        return;
    }

    std::string body;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        body.append(buf, n);
        if (body.size() > 1024 * 1024) break;
    }
    pclose(fp);

    // 如果登录成功 (code=0 且有 url)，解析 cookie 并保存
    // 这里返回原始响应，JS 层处理解析并调用 setCookie
    info.GetReturnValue().Set(body);
}

void BiliAuth::isLoggedIn(JQUTIL_NS::JQFunctionInfo& info) {
    if (!openDB()) { info.GetReturnValue().Set(false); return; }

    std::string cookie;
    bool hasCookie = querySingle("SELECT value FROM cookies WHERE key='bili_cookie';", cookie);
    info.GetReturnValue().Set(hasCookie && !cookie.empty());
}

void BiliAuth::getUserInfo(JQUTIL_NS::JQFunctionInfo& info) {
    if (!openDB()) { info.GetReturnValue().Set(std::string("{}")); return; }

    JSContext* ctx = info.GetContext();
    JSValue obj = JS_NewObject(ctx);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT key, value FROM user_info;";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* k = sqlite3_column_text(stmt, 0);
            const unsigned char* v = sqlite3_column_text(stmt, 1);
            if (k && v) {
                JSValue val = JS_NewString(ctx, reinterpret_cast<const char*>(v));
                JS_SetPropertyStr(ctx, obj, reinterpret_cast<const char*>(k), val);
            }
        }
        sqlite3_finalize(stmt);
    }
    info.GetReturnValue().Set(obj);
}

void BiliAuth::logout(JQUTIL_NS::JQFunctionInfo& info) {
    if (!openDB()) { info.GetReturnValue().Set(false); return; }
    
    bool ok1 = execSQL("DELETE FROM cookies WHERE key='bili_cookie';");
    bool ok2 = execSQL("DELETE FROM user_info;");
    
    info.GetReturnValue().Set(ok1 && ok2);
}

JQFunctionTemplateRef createBiliAuth(JQModuleEnv* env)
{
    JQFunctionTemplateRef tpl = JQFunctionTemplate::New(env, "BiliAuth");
    tpl->InstanceTemplate()->setObjectCreator([]() {
        static BiliAuth* auth = []() {
            BiliAuth* instance = new BiliAuth();
            instance->REF();
            return instance;
        }();
        return auth;
    });

    tpl->SetProtoMethod("init", &BiliAuth::init);
    tpl->SetProtoMethod("getCookie", &BiliAuth::getCookie);
    tpl->SetProtoMethod("setCookie", &BiliAuth::setCookie);
    tpl->SetProtoMethod("clearCookie", &BiliAuth::clearCookie);
    tpl->SetProtoMethod("getSettings", &BiliAuth::getSettings);
    tpl->SetProtoMethod("setSetting", &BiliAuth::setSetting);
    tpl->SetProtoMethod("generateQrCode", &BiliAuth::generateQrCode);
    tpl->SetProtoMethod("pollQrCode", &BiliAuth::pollQrCode);
    tpl->SetProtoMethod("isLoggedIn", &BiliAuth::isLoggedIn);
    tpl->SetProtoMethod("getUserInfo", &BiliAuth::getUserInfo);
    tpl->SetProtoMethod("logout", &BiliAuth::logout);

    return tpl->CallConstructor();
}

void biliauth_init(JQModuleEnv* env)
{
    env->setModuleExport("biliAuth", createBiliAuth(env));
}

}  // namespace biliauth