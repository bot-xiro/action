#pragma once

#include <string>

namespace gstplayer {
namespace proxy {

// 本地反向代理：把 https/http 直链改写为 127.0.0.1:<port>/?u=<urlencoded 原地址>
// 代理内部用设备自带 curl 转发，附加 B 站防 403 所需的 Referer + 浏览器 UA，
// 并把 HTTP Range（seek）原样透传。开启视频时 ensureStarted 懒启动一次。
//
// 背景（2026-08-11 真机实证）：B 站 CDN（edge.mountaintoys.cn）对无 Referer 的
// 请求直接 403（ChineseBook 系统播放器路径即死于此处）；带
// Referer: https://www.bilibili.com/ 后 curl 实测 200 整包 / 206 Range 均正常。
// souphttpsrc extra-headers 理论上同效，但为彻底排除 header 未生效的不确定性，
// 按用户要求落本地反向代理方案，请求链路上 Referer 由系统 curl 保证携带。

// 幂等启动代理监听线程；失败返回 false（调用方保持原始 uri 直连）。
bool ensureStarted();

// uri 为 http(s):// 且代理可用时返回代理地址，否则原样返回。
std::string maybeRewrite(const std::string& uri);

}  // namespace proxy
}  // namespace gstplayer