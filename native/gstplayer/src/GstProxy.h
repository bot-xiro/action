#pragma once

#include <string>

// 【2026-08-16 磁盘分块缓存】
// - 代理侧按固定分块把 Range 内容写入 /userdisk/cache/gstproxy/<hash>/<seg>.dat
// - 下次相同 Range 直接由本地磁盘返回，不再走网络
// - 超出上限按 LRU（last_access）自动淘汰最老分块
// - 线程安全：handleClient 全程互斥，listenLoop 不碰 cache

namespace gstplayer {
namespace proxy {

// 幂等启动代理监听线程；失败返回 false（调用方保持原始 uri 直连）。
bool ensureStarted();

// uri 为 http(s):// 且代理可用时返回代理地址，否则原样返回。
std::string maybeRewrite(const std::string& uri);

}  // namespace proxy
}  // namespace gstplayer