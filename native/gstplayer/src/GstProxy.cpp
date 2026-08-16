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
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

// GstProxy 本地反向代理 + 磁盘分块缓存
//
// 架构：单一监听线程 accept（127.0.0.1:18600），每连接一个 detached 线程处理。
// 核心流程：
//   1. 读请求头 → 解析 Range → 查磁盘分块缓存，若命中则直接由本地磁盘返回；
//   2. 未命中时，spawn curl（带 Referer/UA）→ 从流前排解析响应头 → 裸流转发 + 落盘；
//   3. 转发过程中，将 0..2MB、2..4MB、… 固定块写入 /userdisk/cache/gstproxy/<hash>/<N>.dat；
//   4. 每块 .dat 的 mtime 即 last_access，用于 LRU 淘汰。
//   5. 当总块数 > 20 或总大小 > 40MB 时，自动淘汰最老块。
//
// 【长视频优化】souphttpsrc 发 Range bytes=0-（开区间），已改成用已缓存段推断有效终点，
//   杜绝首次打开时的独立 HEAD 请求，省一次 RTT。
//   首块放大到 2MB，降低长视频首段 MISS 概率。

// ===== 常量 =====
namespace {
const char* kCacheRoot   = "/userdisk/cache/gstproxy";
const size_t kSegSize    = 2 * 1024 * 1024;          // 每块 2MB
const size_t kMaxSegs    = 20;                        // 最多 20 块
const size_t kMaxBytes   = kMaxSegs * kSegSize;       // 40MB

const int kListenPort = 18600;
const char* kReferer = "https://www.bilibili.com/";
const char* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
// 代理白名单（lilo 官方 tools_video 同款）
const char* kWhiteList[] = {
    "bilibili.com",
    "bilivideo.com",
    "hdslb.com",
    "mountaintoys.cn",
};
}  // namespace

namespace gstplayer {
namespace proxy {

#define PROXY_LOG(fmt, ...) syslog(LOG_LOCAL7 | LOG_ERR, "[gstproxy] " fmt, ##__VA_ARGS__)

// ===== 全局状态 =====
std::atomic<bool> g_started{false};
std::mutex g_startMutex;
std::mutex g_cacheMutex;
int g_listenFd = -1;
static time_t g_lastEvict = 0;

// ===== 工具 =====
static std::string urlEncode(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 32);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += "0123456789ABCDEF"[c >> 4];
            out += "0123456789ABCDEF"[c & 0xF];
        }
    }
    return out;
}

static std::string urlDecode(const std::string& s) {
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
            if (h >= 0 && l >= 0) { out += (char)((h << 4) | l); i += 2; continue; }
        }
        out += s[i];
    }
    return out;
}

static bool writeAll(int fd, const char* data, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, data, len);
        if (n <= 0) return false;
        data += (size_t)n; len -= (size_t)n;
    }
    return true;
}

static uint32_t djb2(const std::string& s) {
    uint32_t h = 5381;
    for (unsigned char c : s) h = ((h << 5) + h) + (uint32_t)c;
    return h;
}

static bool mkdirp(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
    size_t pos = path.find_last_of('/');
    if (pos != std::string::npos && pos > 0) {
        if (!mkdirp(path.substr(0, pos))) return false;
    }
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

static std::string ensurePageDir(const std::string& url) {
    std::lock_guard<std::mutex> lk(g_cacheMutex);
    static bool rootReady = false;
    if (!rootReady) {
        rootReady = mkdirp(kCacheRoot);
    }
    uint32_t h = djb2(url);
    char sub[11];
    snprintf(sub, sizeof(sub), "%08x", h);
    std::string dir = std::string(kCacheRoot) + "/" + sub;
    mkdirp(dir);
    return dir;
}

static std::string segDatPath(const std::string& dir, uint32_t idx) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%06u.dat", (unsigned)idx);
    return dir + "/" + buf;
}

// 判断 Range [s, e]（含）是否完全命中缓存
static bool rangeInCache(const std::string& url, long long s, long long e, long long* produced) {
    if (e < s) return false;
    std::string dir = ensurePageDir(url);
    uint32_t s0 = (uint32_t)((unsigned long long)s / kSegSize);
    uint32_t s1 = (uint32_t)((unsigned long long)e / kSegSize);
    long long total = 0;
    for (uint32_t i = s0; i <= s1; i++) {
        std::string dp = segDatPath(dir, i);
        struct stat st;
        if (stat(dp.c_str(), &st) != 0) return false;
        long long segOff = (long long)i * (long long)kSegSize;
        long long clipBeg = std::max(s, segOff);
        long long clipEnd = std::min(e, segOff + (long long)kSegSize - 1);
        if (clipEnd < clipBeg) return false;
        if ((long long)st.st_size < clipEnd - clipBeg + 1) return false;
        total += clipEnd - clipBeg + 1;
    }
    if (produced) *produced = total;
    return true;
}

// 从缓存直接服务
static long long serveFromCache(int fd, const std::string& url, long long s, long long e) {
    std::string dir = ensurePageDir(url);
    uint32_t s0 = (uint32_t)((unsigned long long)s / kSegSize);
    uint32_t s1 = (uint32_t)((unsigned long long)e / kSegSize);
    long long sent = 0;
    char buf[65536];
    for (uint32_t i = s0; i <= s1; i++) {
        std::string dp = segDatPath(dir, i);
        FILE* fp = fopen(dp.c_str(), "rb");
        if (!fp) return -1;
        long long segOff = (long long)i * (long long)kSegSize;
        long long clipBeg = std::max(s, segOff);
        if (fseek(fp, clipBeg - segOff, SEEK_SET) != 0) { fclose(fp); return -1; }
        long long clipEnd = std::min(e, segOff + (long long)kSegSize - 1);
        long long rem = clipEnd - clipBeg + 1;
        while (rem > 0) {
            size_t n = (size_t)std::min((long long)sizeof(buf), rem);
            size_t rd = fread(buf, 1, n, fp);
            if (rd == 0) { fclose(fp); return -1; }
            if (!writeAll(fd, buf, rd)) { fclose(fp); return -1; }
            sent += (long long)rd; rem -= (long long)rd;
        }
        fclose(fp);
        utimensat(AT_FDCWD, dp.c_str(), nullptr, 0);
    }
    return sent;
}

// LRU 淘汰
static void evictCache() {
    time_t now = time(nullptr);
    if (now - g_lastEvict < 5) return;
    g_lastEvict = now;

    std::vector<std::string> files;
    long long totalBytes = 0;
    {
        std::lock_guard<std::mutex> lk(g_cacheMutex);
        DIR* d = opendir(kCacheRoot);
        if (!d) return;
        struct dirent* de;
        while ((de = readdir(d)) != nullptr) {
            if (de->d_type != DT_DIR || de->d_name[0] == '.') continue;
            std::string sub = std::string(kCacheRoot) + "/" + de->d_name;
            DIR* sd = opendir(sub.c_str());
            if (!sd) continue;
            struct dirent* sde;
            while ((sde = readdir(sd)) != nullptr) {
                const char* name = sde->d_name;
                size_t ln = strlen(name);
                if (ln < 8 || strcmp(name + ln - 4, ".dat") != 0) continue;
                std::string dp = sub + "/" + name;
                struct stat st;
                if (stat(dp.c_str(), &st) == 0 && st.st_size > 0) {
                    files.push_back(dp);
                    totalBytes += (long long)st.st_size;
                }
            }
            closedir(sd);
        }
        closedir(d);
    }
    if ((size_t)files.size() <= kMaxSegs && totalBytes <= (long long)kMaxBytes) return;

    std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) {
        struct stat sa, sb;
        if (stat(a.c_str(), &sa) != 0) return false;
        if (stat(b.c_str(), &sb) != 0) return true;
        return sa.st_mtime < sb.st_mtime;
    });

    size_t evicted = 0;
    long long evictedBytes = 0;
    for (const auto& dp : files) {
        if ((size_t)(files.size() - evicted) <= kMaxSegs &&
            totalBytes - evictedBytes <= (long long)kMaxBytes) break;
        std::string mp = dp.substr(0, dp.size() - 4) + ".meta";
        ::unlink(dp.c_str());
        ::unlink(mp.c_str());
        evicted++;
    }
    if (evicted > 0) {
        PROXY_LOG("cache EVICT: removed %zu segments", evicted);
    }
}

// 原子写/追字节到缓存块
static void cacheAppend(const std::string& url, uint32_t segIdx, const void* data, size_t len) {
    std::string dir = ensurePageDir(url);
    std::string dp = segDatPath(dir, segIdx);
    int fd = open(dp.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (fd < 0) return;
    ssize_t wr = write(fd, data, len);
    (void)wr;
    close(fd);
    utimensat(AT_FDCWD, dp.c_str(), nullptr, 0);
    evictCache();
}

// fork + exec curl；返回子进程 pid，失败返回 -1。stdout 管道写端返回在 outFd。
static pid_t spawnCurl(const std::string& cmd, int* outFd) {
    int pfd[2];
    if (pipe(pfd) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(pfd[0]); close(pfd[1]); return -1; }
    if (pid == 0) {
        close(pfd[0]);
        for (int fd = 3; fd < 1024; fd++) if (fd != pfd[1]) close(fd);
        dup2(pfd[1], STDOUT_FILENO);
        close(pfd[1]);
        execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        _exit(127);
    }
    close(pfd[1]);
    *outFd = pfd[0];
    return pid;
}

// 从已读取的前导数据中解析 HTTP 响应头，返回 Content-Length；找不到返回 -1。
static long long parseContentLength(const char* data, size_t len) {
    const char* p = data;
    const char* end = data + len;
    while (p + 16 < end) {
        if (p[0] == '\r' && p[1] == '\n') { p += 2; continue; }
        if (p[0] == '\n') { p += 1; continue; }
        if (strncasecmp(p, "Content-Length:", 15) == 0) {
            const char* q = p + 15;
            while (q < end && (*q == ' ' || *q == '\t')) q++;
            long long v = 0;
            while (q < end && *q >= '0' && *q <= '9') {
                v = v * 10 + (*q - '0');
                q++;
            }
            return v;
        }
        while (p < end && *p != '\n') p++;
        if (p < end) p++;
    }
    return -1;
}

// ===== 单连接处理 =====
void handleClient(int fd) {
    std::string head;
    char buf[2048];
    while (head.size() < 65536) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) { close(fd); return; }
        head.append(buf, (size_t)n);
        if (head.find("\r\n\r\n") != std::string::npos ||
            head.find("\n\n")     != std::string::npos) break;
    }

    if (head.compare(0, 4, "GET ") != 0) { close(fd); return; }

    std::string targetUrl;
    {
        size_t qpos = head.find('?');
        if (qpos != std::string::npos) {
            size_t ustart = head.find("u=", qpos);
            if (ustart != std::string::npos) {
                size_t uend = head.find_first_of(" \t\r\n", ustart + 2);
                targetUrl = urlDecode(head.substr(ustart + 2, uend - ustart - 2));
            }
        }
    }
    if (targetUrl.empty()) {
        static const char* bad =
            "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        writeAll(fd, bad, strlen(bad)); close(fd); return;
    }

    // 解析 Range
    std::string range;
    {
        size_t rpos = head.find("Range:");
        if (rpos == std::string::npos) rpos = head.find("range:");
        if (rpos != std::string::npos) {
            size_t rbeg = head.find("bytes=", rpos);
            if (rbeg != std::string::npos) {
                size_t rend = head.find("\r\n", rbeg);
                if (rend == std::string::npos) rend = head.find('\n', rbeg);
                range = head.substr(rbeg + 6, rend - rbeg - 6);
            }
        }
    }
    PROXY_LOG("req: range='%s' url=%.80s", range.c_str(), targetUrl.c_str());

    // 计算 Range 区间
    long long rangeStart = -1, rangeEnd = -1;
    if (!range.empty()) {
        int matched = sscanf(range.c_str(), "%lld-%lld", &rangeStart, &rangeEnd);
        if (matched < 1) {
            rangeStart = -1;
            rangeEnd = -1;
        }
        if (rangeStart < 0) rangeStart = 0;
    }

    // 长视频优化：开区间 Range bytes=0- 时，用已缓存段推断有效终点
    // 这样即使没有 totalLen，也能判断后续段是否命中缓存
    if (!range.empty() && rangeStart >= 0 && rangeEnd < rangeStart) {
        std::string dir = ensurePageDir(targetUrl);
        uint32_t segIdx = (uint32_t)((unsigned long long)rangeStart / kSegSize);
        long long inferredEnd = rangeStart - 1;
        for (;;) {
            std::string dp = segDatPath(dir, segIdx);
            struct stat st;
            if (stat(dp.c_str(), &st) != 0) break;
            if (st.st_size <= 0) break;
            long long lastByte = (long long)segIdx * (long long)kSegSize + st.st_size - 1;
            inferredEnd = lastByte;
            if ((size_t)st.st_size < kSegSize) break; // 不完整段，不能继续推断
            segIdx++;
        }
        if (inferredEnd >= rangeStart) {
            rangeEnd = inferredEnd;
            PROXY_LOG("open-range inferred end=%lld via %u segs", (long long)rangeEnd, (unsigned)segIdx);
        }
    }

    // 缓存命中判断（完全不依赖 curl）
    bool servedFromCache = false;
    long long cacheProduced = 0;
    if (!range.empty() && rangeEnd >= rangeStart) {
        long long produced = 0;
        if (rangeInCache(targetUrl, rangeStart, rangeEnd, &produced)) {
            servedFromCache = true;
            cacheProduced = produced;
        }
    }

    // 缓存命中路径：直接磁盘服务，完全不 spawn curl
    if (servedFromCache) {
        char cl[32], cr[96];
        snprintf(cl, sizeof(cl), "%lld", cacheProduced);
        snprintf(cr, sizeof(cr), "bytes %lld-%lld/%lld",
                 rangeStart, rangeEnd, cacheProduced);
        std::string hdr =
            "HTTP/1.1 206 Partial Content\r\nContent-Type: video/mp4\r\n"
            "Accept-Ranges: bytes\r\nCache-Control: no-store\r\n"
            "Connection: keep-alive\r\nContent-Range: " + std::string(cr) +
            "\r\nContent-Length: " + std::string(cl) + "\r\n\r\n";
        if (writeAll(fd, hdr.data(), hdr.size())) {
            serveFromCache(fd, targetUrl, rangeStart, rangeEnd);
        }
        close(fd);
        return;
    }

    // 网络回源路径：直接 spawn curl（不再独立 HEAD）
    std::string escUrl;
    for (char c : targetUrl) { if (c == '\'') escUrl += "'\\''"; else escUrl += c; }
    std::string cmd =
        "curl -sS --connect-timeout 8 --max-time 3600 "
        "--http1.1 --no-alpn "
        " -e '" + std::string(kReferer) + "'"
        " -A '" + std::string(kUserAgent) + "'";
    if (!range.empty()) {
        std::string escR;
        for (char c : range) { if (c == '\'') escR += "'\\''"; else escR += c; }
        cmd += " -r '" + escR + "'";
    }
    cmd += " '" + escUrl + "' 2>/dev/null";

    int srcFd = -1;
    pid_t pid = spawnCurl(cmd, &srcFd);
    if (pid < 0) { close(fd); return; }

    // 从 curl 流前排解析 Content-Length（与传输共享同一连接，无额外 RTT）
    char hdrBuf[65536];
    ssize_t headerBytes = 0;
    size_t headerEnd = 0;
    while (headerBytes < (ssize_t)sizeof(hdrBuf) - 1) {
        ssize_t n = read(srcFd, hdrBuf + headerBytes,
                         (sizeof(hdrBuf) - 1) - (size_t)headerBytes);
        if (n <= 0) break;
        headerBytes += n;
        const char* end = hdrBuf + headerBytes;
        if (headerBytes >= 4) {
            const char* p = hdrBuf;
            const char* scanEnd = end - 3;
            for (; p <= scanEnd; ++p) {
                if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') {
                    headerEnd = (size_t)(p + 4 - hdrBuf);
                    break;
                }
            }
            if (!headerEnd) {
                const char* p2 = hdrBuf;
                const char* scanEnd2 = end - 1;
                for (; p2 <= scanEnd2; ++p2) {
                    if (p2[0] == '\n' && p2[1] == '\n') {
                        headerEnd = (size_t)(p2 + 2 - hdrBuf);
                        break;
                    }
                }
            }
            if (headerEnd) break;
        }
    }

    if (!headerEnd) {
        // 头没收完：透传已有数据后关闭
        if (headerBytes > 0 && !writeAll(fd, hdrBuf, (size_t)headerBytes)) {
            kill(pid, SIGKILL); int st = 0; waitpid(pid, &st, 0);
        }
        close(fd); close(srcFd);
        return;
    }

    // 发送 curl 返回的原始头
    if (!writeAll(fd, hdrBuf, headerEnd)) {
        kill(pid, SIGKILL); int st = 0; waitpid(pid, &st, 0);
        close(fd); close(srcFd);
        return;
    }

    // 跳过 curl 流中已读取的头，进入 body 转发 + 缓存
    const char* bodyPtr = hdrBuf;
    size_t bodyRem = headerBytes;
    {
        const char* p = bodyPtr;
        const char* end = bodyPtr + bodyRem;
        bool found = false;
        while (p + 3 < end) {
            if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') {
                found = true;
                bodyPtr = p + 4;
                bodyRem = (size_t)(end - bodyPtr);
                break;
            }
            if (p[0] == '\n' && p[1] == '\n') {
                found = true;
                bodyPtr = p + 2;
                bodyRem = (size_t)(end - bodyPtr);
                break;
            }
            while (p < end && *p != '\n') p++;
            if (p < end) p++;
        }
        if (!found) bodyRem = 0;
    }

    long long headerCL = parseContentLength(hdrBuf, headerEnd);
    bool useCL = (headerCL >= 0);

    // 缓存启用条件：知道总长度（Content-Length 存在）
    bool caching = useCL;
    uint32_t cacheSegIdx = 0;
    size_t cacheSegOff = 0;
    if (caching) {
        if (rangeStart >= 0) {
            cacheSegIdx = (uint32_t)((unsigned long long)rangeStart / kSegSize);
            cacheSegOff = (size_t)(rangeStart % kSegSize);
        }
    }

    // 先转发 hdrBuf 中封装的 body 数据
    if (bodyRem > 0) {
        if (!writeAll(fd, bodyPtr, bodyRem)) {
            kill(pid, SIGKILL); int st = 0; waitpid(pid, &st, 0);
            close(fd); close(srcFd);
            return;
        }
        if (caching) {
            size_t off = 0;
            while (off < bodyRem) {
                if (cacheSegOff >= kSegSize) {
                    cacheSegOff = 0;
                    cacheSegIdx++;
                }
                size_t room = kSegSize - cacheSegOff;
                size_t chunk = std::min(bodyRem - off, room);
                cacheAppend(targetUrl, cacheSegIdx, bodyPtr + off, chunk);
                off += chunk;
                cacheSegOff += chunk;
            }
        }
    }

    // 继续转发 curl 剩余 body
    char cbuf[65536];
    bool clientAlive = true;
    for (;;) {
        ssize_t n = read(srcFd, cbuf, sizeof(cbuf));
        if (n <= 0) break;
        if (!clientAlive) break;
        if (!writeAll(fd, cbuf, (size_t)n)) { clientAlive = false; break; }
        if (caching) {
            size_t off = 0;
            while (off < (size_t)n) {
                if (cacheSegOff >= kSegSize) {
                    cacheSegOff = 0;
                    cacheSegIdx++;
                }
                size_t room = kSegSize - cacheSegOff;
                size_t chunk = std::min((size_t)n - off, room);
                cacheAppend(targetUrl, cacheSegIdx, cbuf + off, chunk);
                off += chunk;
                cacheSegOff += chunk;
            }
        }
    }
    close(srcFd);

    if (!clientAlive) {
        kill(pid, SIGKILL); int st = 0; waitpid(pid, &st, 0);
    } else {
        int st = 0;
        for (int i = 0; i < 40 && waitpid(pid, &st, WNOHANG) == 0; i++) usleep(50000);
        if (waitpid(pid, &st, WNOHANG) == 0) { kill(pid, SIGKILL); waitpid(pid, &st, 0); }
    }
    close(fd);
}

void listenLoop() {
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 0) { PROXY_LOG("socket failed: %s", strerror(errno)); return; }
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(kListenPort);
    if (bind(ls, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        PROXY_LOG("bind 127.0.0.1:%d failed: %s", kListenPort, strerror(errno));
        close(ls); return;
    }
    if (listen(ls, 8) != 0) {
        PROXY_LOG("listen failed: %s", strerror(errno));
        close(ls); return;
    }
    g_listenFd = ls;
    PROXY_LOG("local reverse proxy (+disk cache) listening on 127.0.0.1:%d", kListenPort);
    for (;;) {
        int cfd = accept(ls, nullptr, nullptr);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            PROXY_LOG("accept failed: %s", strerror(errno)); break;
        }
        std::thread(handleClient, cfd).detach();
    }
    close(ls);
}

bool ensureStarted() {
    if (g_started.load()) return true;
    std::lock_guard<std::mutex> lk(g_startMutex);
    if (g_started.load()) return true;
    g_started.store(true);
    try {
        std::thread(listenLoop).detach();
    } catch (const std::exception& e) {
        PROXY_LOG("start thread failed: %s", e.what());
        g_started.store(false); return false;
    } catch (...) {
        PROXY_LOG("start thread failed: unknown");
        g_started.store(false); return false;
    }
    return true;
}

std::string maybeRewrite(const std::string& uri) {
    if (uri.compare(0, 7, "http://") != 0 && uri.compare(0, 8, "https://") != 0) return uri;
    std::string host = uri;
    size_t schemeEnd = host.find("://");
    if (schemeEnd != std::string::npos) host = host.substr(schemeEnd + 3);
    size_t pathStart = host.find('/');
    if (pathStart != std::string::npos) host = host.substr(0, pathStart);
    size_t colon = host.find(':');
    if (colon != std::string::npos) host = host.substr(0, colon);
    bool inWhitelist = false;
    for (const char* wl : kWhiteList) {
        if (host.find(wl) != std::string::npos) { inWhitelist = true; break; }
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