#include "GstProxy.h"

#include <syslog.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>

#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// GstProxy 本地反向代理（集成在 libjsapi_gstplayer.so 内）
//
// 架构：单一监听线程 accept（127.0.0.1:18600），每连接一个 detached 线程处理。
// 处理流程：读 HTTP 请求头 → 解析 /?u=<urlencoded 原始地址> 与 Range →
// fork + exec /bin/sh -c "curl ..."（带 Referer/UA）→ stdout 管道 → chunked
// 转发给 souphttpsrc。客户端断开时 SIGKILL curl 子进程，不留僵尸下载。
//
// 选型说明：
//  - 不用 popen：拿不到子进程 PID，断连时无法终止 curl。
//  - 不用多路复用/事件循环：连接数极少（gstplayer 单实例、偶发重连/seek），
//    每连接一线程简单可靠，RK3562 双核 A55 完全承受。
//  - fork 后立即 exec（curl），子进程无 pthread 使用，POSIX 安全。
//  - curl 由设备自带（/bin/curl），带 -e/-A 完成防 403 头注入。

namespace gstplayer {
namespace proxy {

// 与 GstPlayer.cpp 一致，走 local7 设施（→ /data/applog/YD_PEN_APP.log）
#define PROXY_LOG(fmt, ...) syslog(LOG_LOCAL7 | LOG_ERR, "[gstproxy] " fmt, ##__VA_ARGS__)

namespace {

const int kListenPort = 18600;          // 固定端口（写死在 .so 与日志中，便于排查）
const char* kReferer = "https://www.bilibili.com/";
const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
// 代理白名单（lilo 官方 tools_video 同款：B 站 CDN/资源域全部走本地代理 +
// Referer 绕 403；其余域名不代理直连，避免影响其他网络场景）。
// 子串匹配（域名 Contains），覆盖 *.<domain> 与裸 domain。
const char* kWhiteList[] = {
    "bilibili.com",      // api/www/upos 等
    "bilivideo.com",     // B 站视频 CDN
    "hdslb.com",         // 封面/静态资源
    "mountaintoys.cn",   // B 站 edge CDN（真机 403 实证域）
};

std::atomic<bool> g_started{false};
std::mutex g_startMutex;
int g_listenFd = -1;  // 子进程需关闭的继承 fd（见 spawnCurl 说明）

const char* kHex = "0123456789ABCDEF";

std::string urlEncode(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 32);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0xF];
        }
    }
    return out;
}

std::string urlDecode(const std::string& s)
{
    auto hexv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int h = hexv(s[i + 1]);
            int l = hexv(s[i + 2]);
            if (h >= 0 && l >= 0) {
                out += (char)((h << 4) | l);
                i += 2;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

bool writeAll(int fd, const char* data, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, data + off, len - off);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}

// fork + exec curl；返回子进程 pid，失败返回 -1。stdout 管道写端返回在 outFd。
// 子进程先关闭除 stdout 外全部继承 fd（含 listen socket / 客户端 socket），
// 避免 fd 泄漏到 curl 生命周期之外。
pid_t spawnCurl(const std::string& cmd, int* outFd)
{
    int pfd[2];
    if (pipe(pfd) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return -1;
    }
    if (pid == 0) {
        // 子进程：只保留 stdout 管道写端
        close(pfd[0]);
        for (int fd = 3; fd < 1024; fd++) {
            if (fd != pfd[1]) close(fd);
        }
        dup2(pfd[1], STDOUT_FILENO);
        close(pfd[1]);
        execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        _exit(127);
    }
    close(pfd[1]);
    *outFd = pfd[0];
    return pid;
}

// 处理单个客户端连接：读请求头 → curl 转发（chunked）
void handleClient(int fd)
{
    std::string head;
    char buf[2048];
    // 请求头最大 64KB（B 站直链 sign 参数较长，初始 Range 请求头约 1KB，足够）
    while (head.size() < 65536) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) {
            close(fd);
            return;
        }
        head.append(buf, (size_t)n);
        if (head.find("\r\n\r\n") != std::string::npos ||
            head.find("\n\n") != std::string::npos) {
            break;
        }
    }

    // 解析请求行: GET /?u=<enc> HTTP/1.1
    if (head.compare(0, 4, "GET ") != 0) {
        close(fd);
        return;
    }
    std::string targetUrl;
    size_t qpos = head.find('?');
    if (qpos != std::string::npos) {
        size_t ustart = head.find("u=", qpos);
        if (ustart != std::string::npos) {
            size_t uend = head.find_first_of(" \t\r\n", ustart + 2);
            targetUrl = urlDecode(head.substr(ustart + 2, uend - ustart - 2));
        }
    }
    if (targetUrl.empty()) {
        const char* bad =
            "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        writeAll(fd, bad, strlen(bad));
        close(fd);
        return;
    }

    // Range: bytes=xxx（行尾 \r\n 判定，兼容 \n 仅作容错）
    std::string range;
    size_t rpos = head.find("Range:");
    if (rpos == std::string::npos) rpos = head.find("range:");
    if (rpos != std::string::npos) {
        size_t rbeg = head.find("bytes=", rpos);
        if (rbeg != std::string::npos) {
            size_t rend = head.find("\r\n", rbeg);
            if (rend == std::string::npos) rend = head.find("\n", rbeg);
            range = head.substr(rbeg + 6, rend - rbeg - 6);
        }
    }
    PROXY_LOG("req: range='%s' url=%.80s", range.c_str(), targetUrl.c_str());

    // 组装 curl 命令（POSIX sh 单引号包裹，url 内单引号按 sh 拼接规则转义）
    std::string escUrl;
    for (char c : targetUrl) {
        if (c == '\'') {
            escUrl += "'\\''";
        } else {
            escUrl += c;
        }
    }
    std::string cmd =
        "curl -sS --connect-timeout 8 --max-time 3600"
        " -e '" + std::string(kReferer) + "'"
        " -A '" + std::string(kUserAgent) + "'";
    if (!range.empty()) {
        std::string escRange;
        for (char c : range) {
            if (c == '\'') escRange += "'\\''";
            else escRange += c;
        }
        cmd += " -r '" + escRange + "'";
    }
    cmd += " '" + escUrl + "' 2>/dev/null";

    // 响应头：chunked 流式（不过 Content-Length，长度由 curl 决定、未知）。
    // 【seek 修复 2026-08-14】必须声明 Accept-Ranges: bytes（+ 206 的 Content-Range），
    // 否则 souphttpsrc 判定源不可 seek → FLUSH seek 后源不重启 → 位置回退。
    std::string hdr;
    if (range.empty()) {
        hdr = "HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\n"
              "Accept-Ranges: bytes\r\n"
              "Cache-Control: no-store\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
    } else {
        hdr = "HTTP/1.1 206 Partial Content\r\nContent-Type: video/mp4\r\n"
              "Accept-Ranges: bytes\r\n"
              "Content-Range: bytes " + range + "/*\r\n"
              "Cache-Control: no-store\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
    }
    if (!writeAll(fd, hdr.data(), hdr.size())) {
        close(fd);
        return;
    }

    int srcFd = -1;
    pid_t pid = spawnCurl(cmd, &srcFd);
    if (pid < 0) {
        const char* term = "0\r\n\r\n";
        writeAll(fd, term, strlen(term));
        close(fd);
        return;
    }

    // 管道 → chunked 转发；客户端断开（写失败）立即终止 curl
    char cbuf[65536];
    bool clientAlive = true;
    for (;;) {
        ssize_t n = read(srcFd, cbuf, sizeof(cbuf));
        if (n <= 0) break;
        char hb[32];
        int hl = snprintf(hb, sizeof(hb), "%zx\r\n", (size_t)n);
        if (!clientAlive) break;
        if (!writeAll(fd, hb, hl) ||
            !writeAll(fd, cbuf, (size_t)n) ||
            !writeAll(fd, "\r\n", 2)) {
            clientAlive = false;
            break;
        }
    }
    close(srcFd);
    if (clientAlive) {
        writeAll(fd, "0\r\n\r\n", 5);
    } else {
        // 客户端已断：杀 curl 避免后台空下载
        kill(pid, SIGKILL);
        int st = 0;
        waitpid(pid, &st, 0);
    }
    // 正常结束也收尸（有界等待，防 curl 卡死拖线程）
    if (clientAlive) {
        int st = 0;
        for (int i = 0; i < 40 && waitpid(pid, &st, WNOHANG) == 0; i++) {
            usleep(50000);  // 最长等 2s
        }
        if (waitpid(pid, &st, WNOHANG) == 0) {
            kill(pid, SIGKILL);
            waitpid(pid, &st, 0);
        }
    }
    close(fd);
}

void listenLoop()
{
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 0) {
        PROXY_LOG("socket failed: %s", strerror(errno));
        return;
    }
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(kListenPort);
    if (bind(ls, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        PROXY_LOG("bind 127.0.0.1:%d failed: %s", kListenPort, strerror(errno));
        close(ls);
        return;
    }
    if (listen(ls, 8) != 0) {
        PROXY_LOG("listen failed: %s", strerror(errno));
        close(ls);
        return;
    }
    g_listenFd = ls;
    PROXY_LOG("local reverse proxy listening on 127.0.0.1:%d", kListenPort);
    for (;;) {
        int cfd = accept(ls, nullptr, nullptr);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            PROXY_LOG("accept failed: %s", strerror(errno));
            break;
        }
        std::thread(handleClient, cfd).detach();
    }
    close(ls);
}

}  // namespace

bool ensureStarted()
{
    if (g_started.load()) return true;
    std::lock_guard<std::mutex> lk(g_startMutex);
    if (g_started.load()) return true;
    g_started.store(true);  // 先置位防并发重入；失败则复位
    try {
        std::thread(listenLoop).detach();
    } catch (const std::exception& e) {
        PROXY_LOG("start thread failed: %s", e.what());
        g_started.store(false);
        return false;
    } catch (...) {
        PROXY_LOG("start thread failed: unknown");
        g_started.store(false);
        return false;
    }
    return true;
}

std::string maybeRewrite(const std::string& uri)
{
    if (uri.compare(0, 7, "http://") != 0 && uri.compare(0, 8, "https://") != 0) {
        return uri;  // 本地文件 / 其他协议不代理
    }
    // 白名单校验：仅 B 站相关域走代理（含子域；host 取 :// 到第一个 / 之间）
    std::string host = uri;
    size_t schemeEnd = host.find("://");
    if (schemeEnd != std::string::npos) host = host.substr(schemeEnd + 3);
    size_t pathStart = host.find('/');
    if (pathStart != std::string::npos) host = host.substr(0, pathStart);
    size_t colon = host.find(':');
    if (colon != std::string::npos) host = host.substr(0, colon);  // 去端口
    bool inWhitelist = false;
    for (const char* wl : kWhiteList) {
        if (host.find(wl) != std::string::npos) {
            inWhitelist = true;
            break;
        }
    }
    if (!inWhitelist) {
        PROXY_LOG("host '%s' not in whitelist, keep direct", host.c_str());
        return uri;
    }
    if (!ensureStarted()) {
        PROXY_LOG("proxy unavailable, keep direct uri");
        return uri;
    }
    return "http://127.0.0.1:" + std::to_string(kListenPort) + "/?u=" + urlEncode(uri);
}

}  // namespace proxy
}  // namespace gstplayer