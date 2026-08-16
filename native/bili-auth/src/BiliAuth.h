#pragma once

#include "jqutil_v2/jqutil.h"

#include <atomic>
#include <mutex>
#include <string>
#include <sqlite3.h>

namespace biliauth {

// BiliAuth: B站登录认证与本地数据库存储模块
//
// JS 侧使用（单例实例，无需 new）：
//   import { biliAuth } from 'bili-auth'
//   biliAuth.init()                              // 初始化数据库
//   biliAuth.getCookie()                         // 获取保存的 cookie 字符串
//   biliAuth.setCookie(cookieStr)                // 保存 cookie 字符串
//   biliAuth.clearCookie()                       // 清除 cookie
//   biliAuth.getSettings()                       // 获取所有设置
//   biliAuth.setSetting(key, value)              // 保存单个设置
//   biliAuth.generateQrCode()                    // 生成登录二维码
//   biliAuth.pollQrCode(qrcodeKey)               // 轮询二维码登录状态
//   biliAuth.isLoggedIn()                        // 检查是否已登录
//   biliAuth.getUserInfo()                       // 获取用户信息
//   biliAuth.logout()                            // 登出
//
// 数据库路径：/userdisk/xiro/bili/app.db
// 表结构：
//   - cookies: key TEXT PRIMARY KEY, value TEXT, updated_at INTEGER
//   - settings: key TEXT PRIMARY KEY, value TEXT, updated_at INTEGER
//   - user_info: key TEXT PRIMARY KEY, value TEXT, updated_at INTEGER

class BiliAuth : public JQUTIL_NS::JQBaseObject {
public:
    BiliAuth();
    virtual ~BiliAuth();

    // ---- JS 方法 ----
    void init(JQUTIL_NS::JQFunctionInfo& info);           // 初始化数据库
    void getCookie(JQUTIL_NS::JQFunctionInfo& info);      // 获取 cookie
    void setCookie(JQUTIL_NS::JQFunctionInfo& info);      // 保存 cookie
    void clearCookie(JQUTIL_NS::JQFunctionInfo& info);    // 清除 cookie
    void getSettings(JQUTIL_NS::JQFunctionInfo& info);    // 获取所有设置
    void setSetting(JQUTIL_NS::JQFunctionInfo& info);     // 保存单个设置
    void generateQrCode(JQUTIL_NS::JQFunctionInfo& info); // 生成登录二维码
    void pollQrCode(JQUTIL_NS::JQFunctionInfo& info);     // 轮询二维码状态
    void isLoggedIn(JQUTIL_NS::JQFunctionInfo& info);     // 检查登录状态
    void getUserInfo(JQUTIL_NS::JQFunctionInfo& info);    // 获取用户信息
    void logout(JQUTIL_NS::JQFunctionInfo& info);         // 登出

private:
    bool openDB();
    void closeDB();
    bool execSQL(const char* sql);
    bool querySingle(const char* sql, std::string& outValue);
    bool queryRow(const char* sql, std::string& outKey, std::string& outValue);

    static const char* DB_PATH;
    sqlite3* db_ = nullptr;
    std::mutex dbMutex_;
    std::atomic<bool> inited_{false};
};

}  // namespace biliauth