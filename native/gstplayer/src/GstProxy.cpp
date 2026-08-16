#include "GstProxy.h"

#include <syslog.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <algorithm>

#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
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

const int kListenPort = 18600;
const char* kReferer = "https://www.bilibili.com/";
const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

// 【分段缓存 2026-08-16】缓存目录与策略
// - 缓存首 5MB（含 moov + 初始 moof/mdat），足以支撑首帧解码 + 短暂播放
// - 缓存键 = URL 的 SHA1 前 16 字符，避免文件名过长/特殊字符
// - 缓存文件：/userdisk/cache/gstproxy/<key>.cache
// - 元数据：<key>.meta 存储 totalLen / cachedEnd / url / timestamp
const char* kCacheDir = "/userdisk/cache/gstproxy";
const size_t kMaxCacheSize = 5 * 1024 * 1024;  // 5MB
const char* kWhiteList[] = {
    "bilibili.com",
    "bilivideo.com",
    "hdslb.com",
    "mountaintoys.cn",
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

// 【分段缓存】SHA1 简易实现（用于缓存键）
// 仅用于生成缓存文件名，不需加密强度，RFC 3174 标准实现
static std::string sha1sum(const std::string& s)
{
    // SHA1 初始哈希值
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    // 预处理：填充
    std::vector<uint8_t> msg(s.begin(), s.end());
    size_t orig_len = msg.size();
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) msg.push_back(0x00);
    uint64_t bit_len = orig_len * 8;
    for (int i = 7; i >= 0; i--) msg.push_back((bit_len >> (i * 8)) & 0xFF);

    auto rol = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
    uint32_t w[80];
    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        for (int i = 0; i < 16; i++) {
            w[i] = (msg[chunk + i*4] << 24) | (msg[chunk + i*4 + 1] << 16) |
                   (msg[chunk + i*4 + 2] << 8) | msg[chunk + i*4 + 3];
        }
        for (int i = 16; i < 80; i++) {
            w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t temp = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = temp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    char buf[41];
    snprintf(buf, sizeof(buf), "%08x%08x%08x%08x%08x",
             h[0], h[1], h[2], h[3], h[4]);
    return std::string(buf, 16);  // 只取前 16 字符作文件名
}

// 【分段缓存】初始化缓存目录
static bool ensureCacheDir()
{
    struct stat st;
    if (stat(kCacheDir, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    // 递归创建（/userdisk/cache/gstproxy）
    std::string cmd = "mkdir -p " + std::string(kCacheDir);
    return system(cmd.c_str()) == 0;
}

// 【分段缓存】生成缓存文件路径
static std::string cachePath(const std::string& url, const char* ext)
{
    std::string key = sha1sum(url);
    return std::string(kCacheDir) + "/" + key + ext;
}

// 【分段缓存】读取元数据
struct CacheMeta {
    long long totalLen = 0;
    size_t cachedEnd = 0;
    std::string url;
    time_t timestamp = 0;
};

static bool loadCacheMeta(const std::string& url, CacheMeta& meta)
{
    std::string mpath = cachePath(url, ".meta");
    FILE* fp = fopen(mpath.c_str(), "rb");
    if (!fp) return false;
    // 简单二进制格式：totalLen(8) + cachedEnd(8) + timestamp(8) + url_len(4) + url
    long long totalLen = 0;
    size_t cachedEnd = 0;
    time_t ts = 0;
    uint32_t url_len = 0;
    if (fread(&totalLen, 8, 1, fp) != 1) { fclose(fp); return false; }
    if (fread(&cachedEnd, 8, 1, fp) != 1) { fclose(fp); return false; }
    if (fread(&ts, 8, 1, fp) != 1) { fclose(fp); return false; }
    if (fread(&url_len, 4, 1, fp) != 1) { fclose(fp); return false; }
    std::string stored_url(url_len, '\0');
    if (fread(&stored_url[0], 1, url_len, fp) != url_len) { fclose(fp); return false; }
    fclose(fp);
    if (stored_url != url) return false;
    meta.totalLen = totalLen;
    meta.cachedEnd = cachedEnd;
    meta.url = stored_url;
    meta.timestamp = ts;
    return true;
}

static void saveCacheMeta(const std::string& url, long long totalLen, size_t cachedEnd)
{
    std::string mpath = cachePath(url, ".meta");
    FILE* fp = fopen(mpath.c_str(), "wb");
    if (!fp) return;
    time_t ts = time(nullptr);
    uint32_t url_len = (uint32_t)url.size();
    fwrite(&totalLen, 8, 1, fp);
    fwrite(&cachedEnd, 8, 1, fp);
    fwrite(&ts, 8, 1, fp);
    fwrite(&url_len, 4, 1, fp);
    fwrite(url.data(), 1, url_len, fp);
    fclose(fp);
}

// 【分段缓存】追加写入缓存数据
static bool appendCacheData(const std::string& url, const void* data, size_t len)
{
    std::string cpath = cachePath(url, ".cache");
    FILE* fp = fopen(cpath.c_str(), "ab");
    if (!fp) return false;
    size_t written = fwrite(data, 1, len, fp);
    fclose(fp);
    return written == len;
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

// HEAD 获取文件总长度（带 Referer/UA 绕 403）。失败返回 0。
// 简单缓存：同一 URL 短时间重复请求（首播+seek）只 HEAD 一次。
long long getContentLength(const std::string& url)
{
    static std::string cachedUrl;
    static long long cachedLen = 0;
    if (cachedUrl == url && cachedLen > 0) return cachedLen;

    std::string cmd = "curl -sI --connect-timeout 8 --max-time 15"
        " -e '" + std::string(kReferer) + "'"
        " -A '" + std::string(kUserAgent) + "'";
    std::string escUrl;
    for (char c : url) {
        if (c == '\'') escUrl += "'\\''";
        else escUrl += c;
    }
    cmd += " '" + escUrl + "' 2>/dev/null";

    long long len = 0;
    FILE* fp = popen(cmd.c_str(), "r");
    if (fp) {
        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            if (strncasecmp(line, "Content-Length:", 15) == 0) {
                len = atoll(line + 15);
                break;
            }
        }
        pclose(fp);
    }
    if (len > 0) {
        cachedUrl = url;
        cachedLen = len;
        PROXY_LOG("HEAD len=%lld for %.80s", len, url.c_str());
    } else {
        PROXY_LOG("HEAD failed (no Content-Length) for %.80s", url.c_str());
    }
    return len;
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

    // 【分段缓存】初始化缓存目录
    ensureCacheDir();

    // 【分段缓存】尝试从缓存服务
    // 仅对无 Range 请求（首次打开）且缓存有效时生效
    if (range.empty()) {
        CacheMeta meta;
        if (loadCacheMeta(targetUrl, meta)) {
            std::string cpath = cachePath(targetUrl, ".cache");
            FILE* cfp = fopen(cpath.c_str(), "rb");
            if (cfp) {
                // 缓存有效：直接发送缓存数据
                PROXY_LOG("cache HIT: serving %zu bytes from cache for %.80s", meta.cachedEnd, targetUrl.c_str());
                char cl[32];
                snprintf(cl, sizeof(cl), "%zu", meta.cachedEnd);
                std::string hdr = "HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\n"
                                  "Accept-Ranges: bytes\r\n"
                                  "Cache-Control: no-store\r\nConnection: close\r\n"
                                  "Content-Length: " + std::string(cl) + "\r\n\r\n";
                if (writeAll(fd, hdr.data(), hdr.size())) {
                    char cbuf[65536];
                    size_t totalSent = 0;
                    while (totalSent < meta.cachedEnd) {
                        size_t toRead = std::min(sizeof(cbuf), meta.cachedEnd - totalSent);
                        size_t n = fread(cbuf, 1, toRead, cfp);
                        if (n == 0) break;
                        if (!writeAll(fd, cbuf, n)) break;
                        totalSent += n;
                    }
                }
                fclose(cfp);
                close(fd);
                return;
            }
        }
    }

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

    // 响应头：Content-Length 方案（2026-08-14 seek 修复）。
    // 【根因】souphttpsrc 判定源可 seek 需要 Content-Length（chunked 传输 length=0
    // → seekable=false → FLUSH seek 后源不重启 → 位置回退，代理请求日志实证：
    // seek 后无任何新 Range 请求）。先 HEAD 拿总长度 L，响应带 Content-Length：
    //   range 空 → 200 + CL=L；range=X- → 206 + Content-Range: bytes X-(L-1)/L + CL=L-X
    // 传输改为裸流（非 chunked）。HEAD 失败时回退 chunked（不 seek 也保播放）。
    std::string hdr;
    long long totalLen = getContentLength(targetUrl);
    bool useCL = (totalLen > 0);
    if (range.empty()) {
        if (useCL) {
            char cl[32];
            snprintf(cl, sizeof(cl), "%lld", totalLen);
            hdr = "HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\n"
                  "Accept-Ranges: bytes\r\n"
                  "Cache-Control: no-store\r\nConnection: close\r\n"
                  "Content-Length: " + std::string(cl) + "\r\n\r\n";
        } else {
            hdr = "HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\n"
                  "Accept-Ranges: bytes\r\n"
                  "Cache-Control: no-store\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
        }
    } else {
        if (useCL) {
            // range 形如 "X-"：取起始字节
            long long start = 0;
            sscanf(range.c_str(), "%lld", &start);
            if (start < 0) start = 0;
            if (start >= totalLen) start = totalLen - 1;
            long long remain = totalLen - start;
            char cl[32], cr[96];
            snprintf(cl, sizeof(cl), "%lld", remain);
            snprintf(cr, sizeof(cr), "bytes %lld-%lld/%lld", start, totalLen - 1, totalLen);
            hdr = "HTTP/1.1 206 Partial Content\r\nContent-Type: video/mp4\r\n"
                  "Accept-Ranges: bytes\r\n"
                  "Content-Range: " + std::string(cr) + "\r\n"
                  "Cache-Control: no-store\r\nConnection: close\r\n"
                  "Content-Length: " + std::string(cl) + "\r\n\r\n";
        } else {
            hdr = "HTTP/1.1 206 Partial Content\r\nContent-Type: video/mp4\r\n"
                  "Accept-Ranges: bytes\r\n"
                  "Content-Range: bytes " + range + "/*\r\n"
                  "Cache-Control: no-store\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
        }
    }
    if (!writeAll(fd, hdr.data(), hdr.size())) {
        close(fd);
        return;
    }

    int srcFd = -1;
    pid_t pid = spawnCurl(cmd, &srcFd);
    if (pid < 0) {
        if (!useCL) writeAll(fd, "0\r\n\r\n", 5);
        close(fd);
        return;
    }

    // 【分段缓存】缓存写入（仅首次无 Range 请求）
    bool caching = range.empty() && useCL;
    FILE* cfp = nullptr;
    size_t cachedBytes = 0;
    if (caching) {
        std::string cpath = cachePath(targetUrl, ".cache");
        cfp = fopen(cpath.c_str(), "wb");
        if (!cfp) caching = false;
    }

    // 管道 → 转发（CL 方案为裸流；chunked 回退路径保留分帧）
    char cbuf[65536];
    bool clientAlive = true;
    for (;;) {
        ssize_t n = read(srcFd, cbuf, sizeof(cbuf));
        if (n <= 0) break;
        if (useCL) {
            if (!clientAlive) break;
            if (!writeAll(fd, cbuf, (size_t)n)) {
                clientAlive = false;
                break;
            }
            // 【分段缓存】同时写入缓存文件（限制 5MB）
            if (caching && cfp) {
                size_t toWrite = std::min((size_t)n, kMaxCacheSize - cachedBytes);
                if (toWrite > 0) {
                    fwrite(cbuf, 1, toWrite, cfp);
                    cachedBytes += toWrite;
                    if (cachedBytes >= kMaxCacheSize) {
                        fclose(cfp);
                        cfp = nullptr;
                        caching = false;
                        saveCacheMeta(targetUrl, totalLen, cachedBytes);
                    }
                }
            }
        } else {
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
    }
    close(srcFd);
    // 【分段缓存】关闭缓存文件并保存元数据
    if (caching && cfp) {
        fclose(cfp);
        cfp = nullptr;
        saveCacheMeta(targetUrl, totalLen, cachedBytes);
        PROXY_LOG("cache SAVED: %zu bytes for %.80s", cachedBytes, targetUrl.c_str());
    }
    if (!useCL && clientAlive) {
        writeAll(fd, "0\r\n\r\n", 5);
    } else if (!clientAlive) {
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